// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamproxy.h"

#include "networkauth.h"
#include "streambuffer.h"
#include "timeshiftbuffer.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <memory>
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

bool isHlsType(const QByteArray &contentType)
{
    // application/vnd.apple.mpegurl, application/x-mpegurl, audio/mpegurl, …
    return contentType.toLower().contains("mpegurl");
}

// Queued for a live player while rewinding; the rest is on disk anyway.
constexpr qint64 s_maxLiveQueue = static_cast<const qint64>(32 * 1024 * 1024);

struct Reader {
    QPointer<QTcpSocket> socket;
    // Plays from the rewind files instead of the live buffer.
    bool fromFile = false;
    qint64 position = 0;
    bool headerSent = false;
};
}

// One stream: fetches it, buffers it, reconnects to the server and, when rewinding,
// writes it to disk. Players attach to it as readers: one live, any number from the files.
class StreamSession : public QObject
{
public:
    StreamSession(const StreamProxy::Target &target,
                  StreamProxy::Timing timing,
                  QNetworkAccessManager *networkManager,
                  StreamProxy::TimeshiftOptions options,
                  QString directory,
                  std::function<void()> changed,
                  QObject *parent)
        : QObject(parent)
        , m_target(target)
        , m_timing(std::move(timing))
        , m_networkManager(networkManager)
        , m_options(std::move(options))
        , m_directory(std::move(directory))
        , m_changed(std::move(changed))
        , m_buffer(target.bufferMode)
    {
        m_stallTimer.setSingleShot(true);
        m_retryTimer.setSingleShot(true);
        connect(&m_stallTimer, &QTimer::timeout, this, &StreamSession::checkStall);
        connect(&m_retryTimer, &QTimer::timeout, this, &StreamSession::request);

        if (m_target.rewindMs > 0 && !m_directory.isEmpty()) {
            const qint64 maxMs = m_target.rewindMs;
            m_buffer.setPacketSink([this, maxMs](const char *data, qint64 size, qint64 mediaMs) {
                if (!m_timeshift) {
                    startTimeshift(maxMs);
                }

                writeTimeshift(data, size, mediaMs);
            });
        }

        request();
    }

    ~StreamSession() override
    {
        dropReply();

        for (const Reader &reader : std::as_const(m_readers)) {
            if (reader.socket) {
                reader.socket->disconnect(this);
                reader.socket->disconnectFromHost();
            }
        }
    }

    StreamSession(const StreamSession &) = delete;
    StreamSession &operator=(const StreamSession &) = delete;

    void attach(QTcpSocket *socket, qint64 behindMs)
    {
        Reader reader{.socket = socket};

        if (behindMs > 0 && m_timeshift
            && m_timeshift->endPosition() > m_timeshift->oldestPosition()) {
            reader.fromFile = true;
            reader.position = m_timeshift->positionAt(m_timeshift->newestMs() - behindMs);
        } else if (m_upstreamDone) {
            respond(socket, "502 Bad Gateway");

            return;
        } else {
            // One live player at a time: a new connection takes over the buffer.
            for (const Reader &old : std::as_const(m_readers)) {
                if (!old.fromFile && old.socket) {
                    old.socket->disconnect(this);
                    old.socket->disconnectFromHost();
                }
            }

            m_readers.removeIf([](const Reader &old) {
                return !old.fromFile;
            });
        }

        connect(socket, &QTcpSocket::bytesWritten, this, &StreamSession::pump);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            detach(socket);
        });
        m_readers.append(reader);

        if (m_typeKnown) {
            sendHeaders();
        }

        pump();
    }

    // Keeps everything from now on, including what the live player didn't get yet.
    bool hold()
    {
        if (m_timeshift) {
            return true;
        }

        if (!m_buffer.isTransportStream() || m_upstreamDone || m_directory.isEmpty()) {
            return false;
        }

        startTimeshift(0);
        const StreamBuffer::PacketSink sink =
            [this](const char *data, qint64 size, qint64 mediaMs) {
                writeTimeshift(data, size, mediaMs);
            };
        m_buffer.replayQueued(sink);
        m_buffer.setPacketSink(sink);

        return true;
    }

    [[nodiscard]] StreamProxy::TimeshiftState state() const
    {
        StreamProxy::TimeshiftState state;
        state.known = m_buffer.formatKnown();

        if (m_timeshift) {
            state.available = m_timeshift->endPosition() > m_timeshift->oldestPosition();
            state.stopped = m_timeshift->stopped();
            state.windowMs = m_timeshift->windowMs();
        }

        return state;
    }

private:
    void startTimeshift(qint64 maxMs)
    {
        TimeshiftBuffer::Options options;
        options.maxMs = maxMs;
        options.minFreeBytes = m_options.minFreeBytes;
        options.chunkMs = m_options.chunkMs;
        options.freeBytes = m_options.freeBytes;
        m_timeshift = std::make_unique<TimeshiftBuffer>(m_directory, options);
    }

    void writeTimeshift(const char *data, qint64 size, qint64 mediaMs)
    {
        const bool wasStopped = m_timeshift->stopped();
        m_timeshift->write(data, size, mediaMs);

        if (!m_announced || m_timeshift->stopped() != wasStopped) {
            m_announced = true;
            m_changed();
        }
    }

    // Writing to disk, or about to once the format is known.
    [[nodiscard]] bool recording() const
    {
        return m_timeshift
            || (m_target.rewindMs > 0 && !m_directory.isEmpty() && !m_buffer.formatKnown());
    }

    // A session that may still be played from lives without players.
    [[nodiscard]] bool keepAlive() const
    {
        return m_timeshift || (recording() && !m_upstreamDone);
    }

    void detach(QTcpSocket *socket)
    {
        m_readers.removeIf([socket](const Reader &reader) {
            return !reader.socket || reader.socket == socket;
        });

        if (m_readers.isEmpty() && !keepAlive()) {
            dropReply();
            deleteLater();
        }
    }

    [[nodiscard]] const Reader *liveReader() const
    {
        for (const Reader &reader : m_readers) {
            if (!reader.fromFile && reader.socket
                && reader.socket->state() == QAbstractSocket::ConnectedState) {
                return &reader;
            }
        }

        return nullptr;
    }

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
        if (!m_typeKnown && !answer()) {
            return;
        }

        m_stallTimer.start(m_timing.stallMs);
        pump();
    }

    // Starts the response to the players. False when they were sent elsewhere.
    bool answer()
    {
        const QByteArray type = m_reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();

        if (isHlsType(type)) {
            if (m_target.username.isEmpty()) {
                // Let the player fetch the playlist and its segments itself.
                const QByteArray location = m_reply->url().toEncoded();
                dropReply();
                m_upstreamDone = true;

                for (const Reader &reader : std::as_const(m_readers)) {
                    if (reader.socket) {
                        respond(reader.socket, "302 Found", "Location: " + location + "\r\n");
                    }
                }

                return false;
            }

            // Needs our credentials: relay it unchanged, as there is nothing to buffer.
            m_passThrough = true;
        }

        m_typeKnown = true;
        m_contentType = type.isEmpty() ? QByteArray("application/octet-stream") : type;
        sendHeaders();

        return true;
    }

    void sendHeaders()
    {
        for (Reader &reader : m_readers) {
            if (!reader.headerSent && reader.socket) {
                reader.headerSent = true;
                reader.socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + m_contentType
                                     + "\r\nConnection: close\r\n\r\n");
            }
        }
    }

    // Moves data from the server to the buffer and the disk, and from there to the players.
    void pump()
    {
        bool progress = true;

        while (progress) {
            progress = false;
            const Reader *live = liveReader();

            if (m_reply && m_typeKnown && m_reply->bytesAvailable() > 0
                && (m_passThrough ? live != nullptr : recording() || !m_buffer.full())) {
                const QByteArray data = m_reply->read(s_chunkSize);
                m_replyBytes += data.size();

                if (m_replyBytes >= s_stableBytes) {
                    m_attempts = 0;
                }

                if (m_passThrough) {
                    live->socket->write(data);
                } else {
                    append(data);
                }

                progress = true;
            }

            live = liveReader();

            if (!m_passThrough && live && live->headerSent
                && live->socket->bytesToWrite() < s_maxPending) {
                const QByteArray data = m_buffer.take(s_chunkSize);

                if (!data.isEmpty()) {
                    live->socket->write(data);
                    progress = true;
                }
            }

            progress = serveFiles() || progress;
        }
    }

    void append(const QByteArray &data)
    {
        m_buffer.append(data);

        // Nobody waits for the live buffer when the disk has it all.
        if (recording()) {
            m_buffer.keepNewest(liveReader() ? s_maxLiveQueue : 0);
        }
    }

    // Feeds the players that play from disk. True when something was sent.
    bool serveFiles()
    {
        if (!m_timeshift) {
            return false;
        }

        bool progress = false;
        QList<QPointer<QTcpSocket>> finished;

        for (Reader &reader : m_readers) {
            if (!reader.fromFile || !reader.socket || !reader.headerSent
                || reader.socket->bytesToWrite() >= s_maxPending) {
                continue;
            }

            // Deleted while the player was paused: continue at the oldest data.
            reader.position = qMax(reader.position, m_timeshift->oldestPosition());
            const QByteArray data = m_timeshift->read(reader.position, s_chunkSize);

            if (!data.isEmpty()) {
                reader.socket->write(data);
                reader.position += data.size();
                progress = true;
            } else if ((m_timeshift->stopped() || m_upstreamDone)
                       && reader.position >= m_timeshift->endPosition()) {
                // Nothing more will come: the player goes back to live.
                finished.append(reader.socket);
            }
        }

        for (const QPointer<QTcpSocket> &socket : std::as_const(finished)) {
            if (socket) {
                socket->disconnectFromHost();
            }
        }

        return progress;
    }

    void finished()
    {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        m_stallTimer.stop();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (!m_typeKnown) {
            m_upstreamDone = true;

            if (reply->error() != QNetworkReply::NoError) {
                for (const Reader &reader : std::as_const(m_readers)) {
                    if (reader.socket) {
                        respond(reader.socket,
                                status >= 400 ? QByteArray::number(status) + " Upstream Error"
                                              : QByteArray("502 Bad Gateway"));
                    }
                }

                return;
            }

            // An empty reply.
            m_reply = reply;
            const bool answered = answer();
            m_reply = nullptr;

            if (answered) {
                closeLive();
            }

            return;
        }

        // What the reply still holds belongs before the gap.
        const QByteArray rest = reply->readAll();

        if (m_passThrough) {
            if (const Reader *live = liveReader()) {
                live->socket->write(rest);
            }

            m_upstreamDone = true;
            closeLive();

            return;
        }

        append(rest);

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
        if (m_readers.isEmpty() && !keepAlive()) {
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
        m_upstreamDone = true;
        m_buffer.endOfData();

        if (const Reader *live = liveReader()) {
            live->socket->write(m_buffer.drain());
        }

        closeLive();
        // Players behind live finish what is on disk first.
        pump();
    }

    void closeLive()
    {
        if (const Reader *live = liveReader()) {
            live->socket->disconnectFromHost();
        }
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

    StreamProxy::Target m_target;
    StreamProxy::Timing m_timing;
    QNetworkAccessManager *m_networkManager;
    StreamProxy::TimeshiftOptions m_options;
    QString m_directory;
    std::function<void()> m_changed;
    StreamBuffer m_buffer;
    std::unique_ptr<TimeshiftBuffer> m_timeshift;
    QList<Reader> m_readers;
    QPointer<QNetworkReply> m_reply;
    QTimer m_stallTimer;
    QTimer m_retryTimer;
    QByteArray m_contentType;
    bool m_typeKnown = false;
    bool m_passThrough = false;
    bool m_upstreamDone = false;
    bool m_announced = false;
    int m_attempts = 0;
    qint64 m_replyBytes = 0;
};

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

StreamProxy::~StreamProxy()
{
    // Before the network manager their replies belong to.
    for (const QPointer<StreamSession> &session : std::as_const(m_sessions)) {
        delete session.data();
    }
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
                        const ServerConfig &server,
                        const Headers &headers,
                        int bufferMode)
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
                  .username = server.username,
                  .password = server.password,
                  .headers = headers,
                  .bufferMode = bufferMode};

    if (!upstream.userName().isEmpty()) {
        target.username = upstream.userName();
        target.password = upstream.password();
        target.url.setUserInfo(QString());
    }

    m_targets.insert(token, target);
    m_order.append(token);
    while (m_order.size() > s_maxTargets) {
        const QString old = m_order.takeFirst();
        m_targets.remove(old);
        delete session(old);
        m_sessions.remove(old);
    }

    return address(token);
}

QUrl StreamProxy::address(const QString &token) const
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(m_server->serverPort());
    url.setPath(QLatin1Char('/') + token);

    return url;
}

QString StreamProxy::tokenOf(const QUrl &relayUrl)
{
    return relayUrl.path().mid(1);
}

StreamSession *StreamProxy::session(const QString &token) const
{
    return m_sessions.value(token).data();
}

void StreamProxy::setRewind(const QUrl &relayUrl, qint64 maxMs)
{
    const auto target = m_targets.find(tokenOf(relayUrl));

    if (target != m_targets.end()) {
        target->rewindMs = qMax<qint64>(0, maxMs);
    }
}

bool StreamProxy::hold(const QUrl &relayUrl)
{
    StreamSession *found = session(tokenOf(relayUrl));

    return found && found->hold();
}

void StreamProxy::end(const QUrl &relayUrl)
{
    const QString token = tokenOf(relayUrl);
    delete session(token);
    m_sessions.remove(token);
}

StreamProxy::TimeshiftState StreamProxy::timeshift(const QUrl &relayUrl) const
{
    const StreamSession *found = session(tokenOf(relayUrl));

    return found ? found->state() : TimeshiftState{};
}

QUrl StreamProxy::behind(const QUrl &relayUrl, qint64 behindMs)
{
    QUrl url = relayUrl;
    url.setQuery(behindMs > 0 ? QStringLiteral("behind=%1").arg(behindMs) : QString());

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

    const QUrl requested = QUrl::fromEncoded(requestLine.at(1));
    const QString token = tokenOf(requested);
    const auto target = m_targets.constFind(token);
    if (target == m_targets.cend()) {
        respond(socket, "404 Not Found");

        return;
    }

    StreamSession *existing = session(token);

    if (!existing) {
        const QString directory = m_timeshiftOptions.root.isEmpty()
            ? QString()
            : m_timeshiftOptions.root + QLatin1Char('/') + token;
        existing = new StreamSession(
            *target,
            m_timing,
            m_networkManager,
            m_timeshiftOptions,
            directory,
            [this, token]() {
                Q_EMIT timeshiftChanged(address(token));
            },
            this);
        m_sessions.insert(token, existing);
    }

    const QUrlQuery query(requested);
    existing->attach(socket, query.queryItemValue(QStringLiteral("behind")).toLongLong());
}
