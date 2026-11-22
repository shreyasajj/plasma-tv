// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamproxy.h"

#include "networkauth.h"
#include "streambuffer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace
{
constexpr int s_maxTargets = 8;
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
            QNetworkAccessManager *networkManager)
        : QObject(socket)
        , m_socket(socket)
        , m_target(target)
        , m_timing(std::move(timing))
        , m_networkManager(networkManager)
        , m_buffer(target.bufferMode)
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
            if (m_target.username.isEmpty() && m_target.headers.isEmpty()) {
                // Let the player fetch the playlist and its segments itself: it needs neither a
                // login nor the source's headers.
                const QByteArray location = m_reply->url().toEncoded();
                dropReply();
                respond(m_socket, "302 Found", "Location: " + location + "\r\n");

                return false;
            }

            // Needs our credentials or headers: relay it unchanged, as there is nothing to buffer.
            // Only the playlist itself goes through the relay: relative segment addresses would
            // resolve against the relay's address and get a 404, and the player fetches absolute
            // ones without credentials. So this only works for playlists with absolute segment
            // addresses on hosts that need no login for them.
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

                if (m_passThrough) {
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
                        int bufferMode,
                        const Headers &headers)
{
    if (!listen()) {
        return {};
    }

    // The random token keeps other local users from fetching streams with our credentials.
    quint32 random[4];
    QRandomGenerator::system()->fillRange(random);
    const QString token = QString::fromLatin1(
        QByteArray(reinterpret_cast<const char *>(random), sizeof(random)).toHex());

    Target target{.url = upstream,
                  .username = username,
                  .password = password,
                  .bufferMode = bufferMode,
                  .headers = headers};

    if (!upstream.userName().isEmpty()) {
        target.username = upstream.userName();
        target.password = upstream.password();
        target.url.setUserInfo(QString());
    }

    m_targets.insert(token, target);
    m_order.append(token);
    while (m_order.size() > s_maxTargets) {
        m_targets.remove(m_order.takeFirst());
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

    const auto target = m_targets.constFind(QString::fromLatin1(requestLine.at(1).mid(1)));
    if (target == m_targets.cend()) {
        respond(socket, "404 Not Found");

        return;
    }

    // Lives as long as the player's connection.
    new Session(socket, *target, m_timing, m_networkManager);
}
