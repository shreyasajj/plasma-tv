// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamproxy.h"

#include "networkauth.h"
#include "streambuffer.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <ranges>
#include <utility>

namespace
{
// Keep recent channel selections available for player retries.
constexpr int s_maxTargets = 8;
// Retain playlist resources across reloads without growing for the whole broadcast.
constexpr int s_maxResources = 512;
// A playlist must fit in memory before its addresses can be rewritten.
constexpr qsizetype s_maxPlaylistBytes = 1024 * 1024LL;
constexpr qsizetype s_maxRequestSize = static_cast<const qsizetype>(16 * 1024);
// Read from the server only while the buffer has room, so memory stays bounded.
constexpr qint64 s_readBufferSize = static_cast<const qint64>(1024 * 1024);
// Handed to the player's connection but not sent yet; more waits until it is.
constexpr qint64 s_maxPending = static_cast<const qint64>(2 * 1024 * 1024);
constexpr qint64 s_chunkSize = static_cast<const qint64>(256 * 1024);
constexpr int s_timeoutMs = 30000;
// A reconnected server has to deliver this much before the retries start over.
constexpr qint64 s_stableBytes = static_cast<const qint64>(1024 * 1024);

void respond(QTcpSocket *socket, const QByteArray &status, const QByteArray &headers = {})
{
    if (socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    socket->write("HTTP/1.1 " + status + "\r\n" + headers
                  + "Content-Length: 0\r\nConnection: close\r\n\r\n");
    socket->disconnectFromHost();
}

// Error pages (a 403 or 503 with an HTML body) must never end up in the stream.
bool succeeded(const QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    return status >= 200 && status < 300;
}

bool isHlsType(const QByteArray &contentType)
{
    // application/vnd.apple.mpegurl, application/x-mpegurl, audio/mpegurl, …
    return contentType.toLower().contains("mpegurl");
}

// One player connection: fetches the stream, buffers it and reconnects to the server.
class Session : public QObject
{
public:
    Session(QTcpSocket *socket,
            const StreamProxy::Target &target,
            StreamProxy::Timing timing,
            QNetworkAccessManager *networkManager,
            std::function<QUrl(const QUrl &)> relayResource)
        : QObject(socket)
        , m_socket(socket)
        , m_target(target)
        , m_timing(std::move(timing))
        , m_networkManager(networkManager)
        , m_buffer(target.bufferMode)
        , m_relayResource(std::move(relayResource))
        , m_passThrough(target.finite)
    {
        m_stallTimer.setSingleShot(true);
        m_retryTimer.setSingleShot(true);
        connect(&m_stallTimer, &QTimer::timeout, this, &Session::checkStall);
        connect(&m_retryTimer, &QTimer::timeout, this, &Session::request);
        connect(socket, &QTcpSocket::bytesWritten, this, &Session::pump);
        // The player stopped (channel change): stop downloading as well.
        connect(socket, &QTcpSocket::disconnected, this, &Session::dropReply);

        request();
    }

private:
    void request()
    {
        QNetworkRequest upstreamRequest(m_target.url);
        upstreamRequest.setTransferTimeout(s_timeoutMs);
        upstreamRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("flimmer/0.1"));
        upstreamRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                     QNetworkRequest::NoLessSafeRedirectPolicy);

        // The source's own headers win, a User-Agent included.
        for (const auto &[name, value] : m_target.headers) {
            upstreamRequest.setRawHeader(name, value);
        }

        m_reply = m_networkManager->get(upstreamRequest);
        // Always set, even empty: otherwise the server's credentials would be used.
        NetworkAuth::setReplyCredentials(m_reply, m_target.username, m_target.password);
        m_reply->setReadBufferSize(s_readBufferSize);
        m_replyBytes = 0;

        QNetworkReply *reply = m_reply;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
            if (reply == m_reply) {
                received();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();

            if (reply == m_reply) {
                finished();
            }
        });
    }

    void received()
    {
        // finished() decides what to do about it; the body is dropped.
        if (!succeeded(m_reply)) {
            m_reply->readAll();

            return;
        }

        if (!m_headerSent && !answer()) {
            return;
        }

        m_stallTimer.start(m_timing.stallMs);
        pump();
    }

    // Starts the response to the player. False when the player was sent elsewhere.
    bool answer()
    {
        const QByteArray type = m_reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

        if (isHlsType(type)) {
            if (m_target.username.isEmpty() && m_target.headers.isEmpty() && !m_target.secretUrl) {
                // Let the player fetch the playlist and its segments itself: it needs neither a
                // login nor the source's headers, and may see the address.
                const QByteArray location = m_reply->url().toEncoded();
                dropReply();
                respond(m_socket, "302 Found", "Location: " + location + "\r\n");

                return false;
            }

            // Resolve child playlists, segments and keys against the final upstream address.
            m_playlist = true;
            m_passThrough = true;
        }

        m_headerSent = true;
        m_socket->write("HTTP/1.1 200 OK\r\nContent-Type: "
                        + (type.isEmpty() ? QByteArray("application/octet-stream") : type)
                        + "\r\nConnection: close\r\n\r\n");

        return true;
    }

    // Moves data from the server to the buffer and from the buffer to the player.
    void pump()
    {
        if (m_socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }

        bool progress = true;

        while (progress) {
            progress = false;

            if (m_reply && m_headerSent && succeeded(m_reply) && m_reply->bytesAvailable() > 0
                && (m_passThrough || !m_buffer.full())) {
                const QByteArray data = m_reply->read(s_chunkSize);
                m_replyBytes += data.size();

                if (m_replyBytes >= s_stableBytes) {
                    m_attempts = 0;
                }

                if (m_playlist) {
                    m_playlistData.append(data);

                    if (m_playlistData.size() > s_maxPlaylistBytes) {
                        dropReply();
                        m_socket->disconnectFromHost();

                        return;
                    }
                } else if (m_passThrough) {
                    m_socket->write(data);
                } else {
                    m_buffer.append(data);
                }

                progress = true;
            }

            if (!m_passThrough && m_socket->bytesToWrite() < s_maxPending) {
                const QByteArray data = m_buffer.take(s_chunkSize);

                if (!data.isEmpty()) {
                    m_socket->write(data);
                    progress = true;
                }
            }
        }
    }

    void finished()
    {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_stallTimer.stop();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (!m_headerSent) {
            if (reply->error() != QNetworkReply::NoError || (status != 0 && !succeeded(reply))) {
                respond(m_socket,
                        status >= 400 ? QByteArray::number(status) + " Upstream Error"
                                      : QByteArray("502 Bad Gateway"));

                return;
            }

            // An empty reply.
            m_reply = reply;
            const bool answered = answer();
            m_reply = nullptr;

            if (answered) {
                m_socket->disconnectFromHost();
            }

            return;
        }

        // What the reply still holds belongs before the gap, unless it is an error page.
        const QByteArray rest = succeeded(reply) ? reply->readAll() : QByteArray();

        if (m_playlist) {
            m_playlistData.append(rest);

            if (reply->error() == QNetworkReply::NoError
                && m_playlistData.size() <= s_maxPlaylistBytes) {
                m_socket->write(rewritePlaylist(reply->url()));
            }

            m_socket->disconnectFromHost();

            return;
        }

        if (m_passThrough) {
            m_socket->write(rest);
            m_socket->disconnectFromHost();

            return;
        }

        m_buffer.append(rest);

        // These won't get better by asking again; the player's own retry gets a fresh address.
        if (status == 401 || status == 403 || status == 404 || status == 410) {
            giveUp();

            return;
        }

        // A reply with a length is a file that is complete now; a live stream has none and only
        // ends when the connection broke.
        if (reply->error() == QNetworkReply::NoError
            && reply->header(QNetworkRequest::ContentLengthHeader).isValid()) {
            giveUp();

            return;
        }

        reconnect();
    }

    QByteArray rewritePlaylist(const QUrl &base)
    {
        QStringList lines = QString::fromUtf8(m_playlistData).split(QLatin1Char('\n'));
        const QRegularExpression uri(QStringLiteral("URI=\"([^\"]+)\""));

        for (QString &line : lines) {
            const QString trimmed = line.trimmed();

            if (trimmed.isEmpty()) {
                continue;
            }

            if (!trimmed.startsWith(QLatin1Char('#'))) {
                line = m_relayResource(base.resolved(QUrl(trimmed))).toString(QUrl::FullyEncoded);

                continue;
            }

            // Keys and initialization segments carry their address in a tag attribute.
            auto matches = uri.globalMatch(line);
            QList<QRegularExpressionMatch> found;

            while (matches.hasNext()) {
                found.append(matches.next());
            }

            for (const QRegularExpressionMatch &match :
                 std::as_const(found) | std::views::reverse) {
                const QString local = m_relayResource(base.resolved(QUrl(match.captured(1))))
                                          .toString(QUrl::FullyEncoded);
                line.replace(match.capturedStart(1), match.capturedLength(1), local);
            }
        }

        return lines.join(QLatin1Char('\n')).toUtf8();
    }

    void checkStall()
    {
        // Data waiting in the reply means the player is slow, not the server.
        if (m_reply && m_reply->bytesAvailable() > 0) {
            m_stallTimer.start(m_timing.stallMs);

            return;
        }

        dropReply();
        reconnect();
    }

    void reconnect()
    {
        if (m_socket->state() != QAbstractSocket::ConnectedState) {
            return;
        }

        m_buffer.cutForReconnect();

        if (m_attempts >= m_timing.retryDelaysMs.size()) {
            giveUp();

            return;
        }

        m_retryTimer.start(m_timing.retryDelaysMs.at(m_attempts++));
    }

    void giveUp()
    {
        m_buffer.endOfData();
        m_socket->write(m_buffer.drain());
        m_socket->disconnectFromHost();
    }

    void dropReply()
    {
        m_stallTimer.stop();
        m_retryTimer.stop();

        if (QNetworkReply *reply = m_reply) {
            m_reply = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }

    QTcpSocket *m_socket;
    StreamProxy::Target m_target;
    StreamProxy::Timing m_timing;
    QNetworkAccessManager *m_networkManager;
    StreamBuffer m_buffer;
    QPointer<QNetworkReply> m_reply;
    QTimer m_stallTimer;
    QTimer m_retryTimer;
    std::function<QUrl(const QUrl &)> m_relayResource;
    QByteArray m_playlistData;
    bool m_playlist = false;
    bool m_headerSent = false;
    bool m_passThrough = false;
    int m_attempts = 0;
    qint64 m_replyBytes = 0;
};
}

StreamProxy::StreamProxy(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_networkManager(new QNetworkAccessManager(this))
{
    NetworkAuth::install(m_networkManager);
    connect(m_server, &QTcpServer::pendingConnectionAvailable, this, [this] {
        while (QTcpSocket *socket = m_server->nextPendingConnection()) {
            handleConnection(socket);
        }
    });
}

bool StreamProxy::isHls(const QUrl &url)
{
    return url.path().endsWith(QLatin1String(".m3u8"), Qt::CaseInsensitive);
}

bool StreamProxy::listen()
{
    if (m_server->isListening()) {
        return true;
    }

    // Loopback only: the relay must never be reachable from the network.
    if (!m_server->listen(QHostAddress::LocalHost)) {
        qWarning() << "Cannot start the stream relay:" << m_server->errorString();
        return false;
    }

    return true;
}

QUrl StreamProxy::relay(const QUrl &upstream,
                        const QString &username,
                        const QString &password,
                        int bufferMode)
{
    return relay(
        {.url = upstream, .username = username, .password = password, .bufferMode = bufferMode});
}

QUrl StreamProxy::relay(Target target)
{
    if (!listen()) {
        return {};
    }

    // The random token keeps other local users from fetching streams with our credentials.
    quint32 random[4];
    QRandomGenerator::system()->fillRange(random);
    const QString token = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(random), sizeof(random)).toHex());

    if (!target.url.userName().isEmpty()) {
        target.username = target.url.userName();
        target.password = target.url.password();
        target.url.setUserInfo(QString());
    }

    m_targets.insert(token, target);
    m_order.append(token);
    while (m_order.size() > s_maxTargets) {
        const QString expired = m_order.takeFirst();
        m_targets.remove(expired);
        m_resources.remove(expired);
        m_resourceOrder.remove(expired);
    }

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(m_server->serverPort());
    url.setPath(QLatin1Char('/') + token);

    return url;
}

void StreamProxy::handleConnection(QTcpSocket *socket)
{
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

    auto const request = std::make_shared<QByteArray>();
    auto const connection = std::make_shared<QMetaObject::Connection>();

    *connection =
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, request, connection] {
            request->append(socket->readAll());
            const qsizetype end = request->indexOf("\r\n\r\n");

            if (end < 0) {
                if (request->size() > s_maxRequestSize) {
                    disconnect(*connection);
                    respond(socket, "431 Request Header Fields Too Large");
                }

                return;
            }

            // Anything the player sends after the request is ignored.
            disconnect(*connection);
            serve(socket, request->left(end));
        });
}

void StreamProxy::serve(QTcpSocket *socket, const QByteArray &request)
{
    const QList<QByteArray> requestLine = request.left(request.indexOf("\r\n")).split(' ');
    if (requestLine.size() < 2 || requestLine.at(0) != "GET") {
        respond(socket, "405 Method Not Allowed");

        return;
    }

    const QStringList path = QString::fromLatin1(requestLine.at(1).mid(1)).split(QLatin1Char('/'));
    const QString token = path.value(0);
    const auto root = m_targets.constFind(token);

    if (root == m_targets.cend() || path.size() > 2) {
        respond(socket, "404 Not Found");

        return;
    }

    Target target = *root;

    if (path.size() == 2) {
        const auto resources = m_resources.constFind(token);

        if (resources == m_resources.cend() || !resources->contains(path.at(1))) {
            respond(socket, "404 Not Found");

            return;
        }

        target = resources->value(path.at(1));
    }

    // Lives as long as the player's connection.
    new Session(socket, target, m_timing, m_networkManager, [this, token, target](const QUrl &url) {
        return resource(token, target, url);
    });
}

QUrl StreamProxy::resource(const QString &token, const Target &parent, const QUrl &upstream)
{
    if (upstream.scheme() != QLatin1String("http") && upstream.scheme() != QLatin1String("https")) {
        return {};
    }

    const QString suffix = upstream.path().section(QLatin1Char('.'), -1);
    const QRegularExpression extension(QStringLiteral("^[A-Za-z0-9]{1,8}$"));
    const QString key =
        QString::fromLatin1(
            QCryptographicHash::hash(upstream.toEncoded(), QCryptographicHash::Sha256).toHex())
        + QLatin1Char('.') + (extension.match(suffix).hasMatch() ? suffix : QStringLiteral("ts"));
    auto &resources = m_resources[token];
    auto &order = m_resourceOrder[token];

    if (!resources.contains(key)) {
        Target target = parent;
        target.url = upstream;
        target.finite = true;
        target.secretUrl = true;
        resources.insert(key, target);
        order.append(key);
    }

    while (order.size() > s_maxResources) {
        resources.remove(order.takeFirst());
    }

    QUrl local;
    local.setScheme(QStringLiteral("http"));
    local.setHost(QStringLiteral("127.0.0.1"));
    local.setPort(m_server->serverPort());
    local.setPath(QLatin1Char('/') + token + QLatin1Char('/') + key);

    return local;
}
