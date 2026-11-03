// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamproxy.h"

#include <QDir>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <functional>

namespace
{
constexpr int s_packetSize = 188;

QByteArray packet(char fill)
{
    QByteArray p(s_packetSize, fill);
    p[0] = 0x47;
    p[1] = 0x01;
    p[2] = 0x00;
    p[3] = 0x10;

    return p;
}

// A packet with a PCR for 'ms', then four without: what a live channel sends in 100 ms.
QByteArray pcrPackets(qint64 ms)
{
    QByteArray p = packet('p');
    const qint64 pcr = ms * 90;
    p[3] = 0x30; // adaptation field and payload
    p[4] = 7;
    p[5] = 0x10; // PCR flag
    p[6] = char(pcr >> 25);
    p[7] = char(pcr >> 17);
    p[8] = char(pcr >> 9);
    p[9] = char(pcr >> 1);
    p[10] = char(((pcr & 1) << 7) | 0x7e);

    QByteArray data = p;

    for (int i = 0; i < 4; ++i) {
        data += packet(char('a' + (ms / 100) % 26));
    }

    return data;
}

// A live channel from 'fromMs' to 'toMs'.
QByteArray channel(qint64 fromMs, qint64 toMs)
{
    QByteArray data;

    for (qint64 ms = fromMs; ms <= toMs; ms += 100) {
        data += pcrPackets(ms);
    }

    return data;
}

// Bytes of channel() from 'fromMs' up to 'ms'.
qsizetype offsetOf(qint64 fromMs, qint64 ms)
{
    return qsizetype((ms - fromMs) / 100 * 5 * s_packetSize);
}

QByteArray packets(int count, char fill = 'a')
{
    QByteArray data;

    for (int i = 0; i < count; ++i) {
        data += packet(fill);
    }

    return data;
}

// Serves scripted replies and remembers what it was asked.
class FakeServer : public QObject
{
public:
    // Called for each request with its number (from 0).
    using Handler = std::function<void(QTcpSocket *, const QByteArray &request, int number)>;

    explicit FakeServer(Handler handler)
        : m_handler(std::move(handler))
    {
        if (!m_server.listen(QHostAddress::LocalHost)) {
            qFatal("Cannot listen on the loopback address");
        }

        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    const QByteArray request = socket->readAll();

                    if (request.isEmpty()) {
                        return;
                    }

                    m_requests.append(request);
                    m_handler(socket, request, int(m_requests.size()) - 1);
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url(const QString &path = QStringLiteral("/stream")) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    QList<QByteArray> requests() const
    {
        return m_requests;
    }

private:
    QTcpServer m_server;
    Handler m_handler;
    QList<QByteArray> m_requests;
};

// What a player does: GET the relay address and read until the connection closes.
struct Player {
    QByteArray response;
    bool closed = false;

    explicit Player(const QUrl &url)
    {
        m_socket.connectToHost(url.host(), quint16(url.port()));
        QObject::connect(&m_socket, &QTcpSocket::connected, &m_socket, [this, url]() {
            m_socket.write("GET " + url.toEncoded(QUrl::RemoveScheme | QUrl::RemoveAuthority)
                           + " HTTP/1.1\r\nHost: x\r\n\r\n");
        });
        QObject::connect(&m_socket, &QTcpSocket::readyRead, &m_socket, [this]() {
            response += m_socket.readAll();
        });
        QObject::connect(&m_socket, &QTcpSocket::disconnected, &m_socket, [this]() {
            response += m_socket.readAll();
            closed = true;
        });
    }

    bool waitClosed(int ms = 10000)
    {
        return QTest::qWaitFor(
            [this]() {
                return closed;
            },
            ms);
    }

    QByteArray body() const
    {
        const qsizetype end = response.indexOf("\r\n\r\n");

        return end < 0 ? QByteArray() : response.mid(end + 4);
    }

    void close()
    {
        m_socket.disconnectFromHost();
    }

    QByteArray head() const
    {
        return response.left(response.indexOf("\r\n\r\n"));
    }

    QTcpSocket m_socket;
};

// A live stream has no length; a finished file does, and the relay doesn't reconnect after it.
void ok(QTcpSocket *socket, const QByteArray &type = "video/mp2t", qint64 length = -1)
{
    socket->write(
        "HTTP/1.1 200 OK\r\nContent-Type: " + type
        + (length >= 0 ? "\r\nContent-Length: " + QByteArray::number(length) : QByteArray())
        + "\r\nConnection: close\r\n\r\n");
}

// Rewinding into a temporary folder, one chunk file per second.
StreamProxy::TimeshiftOptions timeshiftIn(const QTemporaryDir &root)
{
    StreamProxy::TimeshiftOptions options;
    options.root = root.path();
    options.chunkMs = 1000;
    options.freeBytes = [](const QString &) {
        return qint64(1) << 40;
    };

    return options;
}

int sessionFolders(const QTemporaryDir &root)
{
    return int(QDir(root.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot).size());
}

StreamProxy::Timing fast()
{
    StreamProxy::Timing timing;
    timing.stallMs = 400;
    timing.retryDelaysMs = {20, 20, 20};

    return timing;
}

// A server that only sends once isn't taken for a broken connection.
StreamProxy::Timing patient()
{
    StreamProxy::Timing timing = fast();
    timing.stallMs = 60000;

    return timing;
}
}

class StreamProxyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void passesStreamThrough()
    {
        const QByteArray data = packets(40);
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket, "video/mp2t", data.size());
            socket->write(data);
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);
        QVERIFY(!url.isEmpty());
        QCOMPARE(url.host(), QStringLiteral("127.0.0.1"));

        Player player(url);
        QVERIFY(player.waitClosed());
        QVERIFY(player.head().startsWith("HTTP/1.1 200"));
        QVERIFY(player.head().contains("video/mp2t"));
        QCOMPARE(player.body(), data);
        QCOMPARE(server.requests().size(), 1);
    }

    void unknownTokenIs404()
    {
        StreamProxy proxy;
        FakeServer server([](QTcpSocket *, const QByteArray &, int) { });
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);
        QUrl wrong = url;
        wrong.setPath(QStringLiteral("/nope"));

        Player player(wrong);
        QVERIFY(player.waitClosed());
        QVERIFY(player.head().startsWith("HTTP/1.1 404"));
        QVERIFY(server.requests().isEmpty());
    }

    void upstreamErrorIsPassedOn()
    {
        FakeServer server([](QTcpSocket *socket, const QByteArray &, int) {
            socket->write("HTTP/1.1 503 Busy\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        Player player(proxy.relay(server.url(), {}, {}, 0));

        QVERIFY(player.waitClosed());
        QVERIFY(player.head().startsWith("HTTP/1.1 503"));
    }

    void answersLoginChallenge()
    {
        const QByteArray data = packets(10);
        // user:secret
        const QByteArray expected = "Authorization: Basic dXNlcjpzZWNyZXQ=";
        FakeServer server([&](QTcpSocket *socket, const QByteArray &request, int) {
            if (!request.contains(expected)) {
                socket->write(
                    "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic "
                    "realm=\"x\"\r\nContent-Length: 0\r\n\r\n");

                return;
            }

            ok(socket, "video/mp2t", data.size());
            socket->write(data);
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        Player player(
            proxy.relay(server.url(), QStringLiteral("user"), QStringLiteral("secret"), 0));

        QVERIFY(player.waitClosed());
        QCOMPARE(player.body(), data);
    }

    void credentialsInUrlAreUsedAndRemoved()
    {
        const QByteArray data = packets(10);
        FakeServer server([&](QTcpSocket *socket, const QByteArray &request, int) {
            if (!request.contains("Authorization: Basic dXNlcjpzZWNyZXQ=")) {
                socket->write(
                    "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic "
                    "realm=\"x\"\r\nContent-Length: 0\r\n\r\n");

                return;
            }

            ok(socket, "video/mp2t", data.size());
            socket->write(data);
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        QUrl upstream = server.url();
        upstream.setUserName(QStringLiteral("user"));
        upstream.setPassword(QStringLiteral("secret"));
        const QUrl url = proxy.relay(upstream, {}, {}, 0);

        QVERIFY(!url.toString().contains(QLatin1String("secret")));
        QVERIFY(url.userName().isEmpty());

        Player player(url);
        QVERIFY(player.waitClosed());
        QCOMPARE(player.body(), data);
        // The request line never carries the credentials either.
        QVERIFY(!server.requests().constFirst().startsWith("GET http://user"));
    }

    void noCredentialsWithoutUser()
    {
        FakeServer server([](QTcpSocket *socket, const QByteArray &, int) {
            socket->write(
                "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic "
                "realm=\"x\"\r\nContent-Length: 0\r\n\r\n");
        });
        StreamProxy proxy;
        Player player(proxy.relay(server.url(), {}, {}, 0));

        QVERIFY(player.waitClosed());
        QVERIFY(player.head().startsWith("HTTP/1.1 401"));
        QVERIFY(!server.requests().constFirst().contains("Authorization"));
    }

    void hlsPlaylistRedirectsPlayer()
    {
        FakeServer server([](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket, "application/vnd.apple.mpegurl");
            socket->write("#EXTM3U\n#EXT-X-VERSION:3\n");
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        Player player(proxy.relay(server.url(), {}, {}, 0));

        QVERIFY(player.waitClosed());
        QVERIFY(player.head().startsWith("HTTP/1.1 302"));
        QVERIFY(player.head().contains("Location: " + server.url().toEncoded()));
    }

    void detectsHlsByPath()
    {
        QVERIFY(StreamProxy::isHls(QUrl(QStringLiteral("http://a/live/1.m3u8?token=x"))));
        QVERIFY(StreamProxy::isHls(QUrl(QStringLiteral("http://a/live/1.M3U8"))));
        QVERIFY(!StreamProxy::isHls(QUrl(QStringLiteral("http://a/live/1.ts"))));
    }

    void reconnectsWhenUpstreamDrops()
    {
        const QByteArray first = packets(20, 'a');
        const QByteArray second = packets(20, 'b');
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int number) {
            ok(socket);

            if (number == 0) {
                // Breaks off in the middle of a packet.
                socket->write(first + packet('x').left(50));
                socket->disconnectFromHost();

                return;
            }

            socket->write(second);
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        proxy.setTiming(fast());
        Player player(proxy.relay(server.url(), {}, {}, 0));

        // The server only ends the stream after the retries ran out.
        QVERIFY(player.waitClosed());
        QVERIFY(server.requests().size() > 1);
        // The player got one 200 and a seamless stream of whole packets, with no error in between.
        QCOMPARE(player.response.count("HTTP/1.1"), 1);
        QVERIFY(player.body().startsWith(first));
        QVERIFY(player.body().contains(second));
        QCOMPARE(player.body().size() % s_packetSize, 0);
    }

    void reconnectsWhenUpstreamStalls()
    {
        const QByteArray first = packets(10, 'a');
        const QByteArray second = packets(10, 'b');
        QPointer<QTcpSocket> stuck;
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int number) {
            ok(socket);

            if (number == 0) {
                socket->write(first);
                stuck = socket; // never sends more

                return;
            }

            socket->write(second);
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        proxy.setTiming(fast());
        Player player(proxy.relay(server.url(), {}, {}, 0));

        QVERIFY(QTest::qWaitFor(
            [&]() {
                return player.body().contains(second);
            },
            10000));
        QVERIFY(player.body().startsWith(first));
    }

    void givesUpAfterRetriesRunOut()
    {
        const QByteArray data = packets(5);
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int number) {
            if (number == 0) {
                ok(socket);
                socket->write(data);
                socket->disconnectFromHost();

                return;
            }

            // Every reconnect is refused.
            socket->write(
                "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        proxy.setTiming(fast());
        Player player(proxy.relay(server.url(), {}, {}, 0));

        QVERIFY(player.waitClosed());
        // First request plus the three retries.
        QCOMPARE(server.requests().size(), 4);
        QCOMPARE(player.body(), data);
    }

    void doesNotRetryAfterAuthenticationFailure()
    {
        const QByteArray data = packets(5);
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int number) {
            if (number == 0) {
                ok(socket);
                socket->write(data);
                socket->disconnectFromHost();

                return;
            }

            socket->write(
                "HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            socket->disconnectFromHost();
        });
        StreamProxy proxy;
        proxy.setTiming(fast());
        Player player(proxy.relay(server.url(), {}, {}, 0));

        QVERIFY(player.waitClosed());
        QCOMPARE(server.requests().size(), 2);
    }

    void holdsDataUntilBufferIsFull()
    {
        // Two seconds of stream (PCR every 100 ms), sent at once.
        QByteArray data;

        for (int i = 0; i <= 30; ++i) {
            QByteArray p = packet('p');
            const qint64 pcr = qint64(i) * 100 * 90;
            p[3] = 0x30;
            p[4] = 7;
            p[5] = 0x10;
            p[6] = char(pcr >> 25);
            p[7] = char(pcr >> 17);
            p[8] = char(pcr >> 9);
            p[9] = char(pcr >> 1);
            p[10] = char(((pcr & 1) << 7) | 0x7e);
            data += p + packets(3);
        }

        QTcpSocket *held = nullptr;
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket, "video/mp2t", data.size());
            // Only one second, then more later.
            socket->write(data.left(data.size() / 3));
            held = socket;
        });
        StreamProxy proxy;
        Player player(proxy.relay(server.url(), {}, {}, 2));

        QTest::qWait(500);
        // The 2 s buffer isn't reached yet, so only the headers went to the player.
        QVERIFY(player.body().isEmpty());

        QVERIFY(held);
        held->write(data.mid(data.size() / 3));
        held->disconnectFromHost();
        QVERIFY(player.waitClosed());
        QCOMPARE(player.body(), data);
    }

    void rewindKeepsTheSessionAfterThePlayerLeaves()
    {
        const QByteArray data = channel(0, 5000);
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            // Live: the connection stays open.
            ok(socket);
            socket->write(data);
        });
        QTemporaryDir root;
        StreamProxy proxy;
        proxy.setTiming(patient());
        proxy.setTimeshiftOptions(timeshiftIn(root));
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);
        proxy.setRewind(url, 60000);

        auto live = std::make_unique<Player>(url);
        QVERIFY(QTest::qWaitFor([&]() {
            return proxy.timeshift(url).windowMs >= 5000;
        }));
        QVERIFY(proxy.timeshift(url).known);
        QVERIFY(proxy.timeshift(url).available);
        live->close();
        QVERIFY(live->waitClosed());
        live.reset();
        QTest::qWait(100);

        // 3 s behind 5000 ms is 2000 ms, which has a mark of its own.
        Player behind(StreamProxy::behind(url, 3000));
        const QByteArray expected = data.mid(offsetOf(0, 2000));
        QVERIFY(QTest::qWaitFor([&]() {
            return behind.body().size() >= expected.size();
        }));
        QCOMPARE(behind.body(), expected);
        QVERIFY(behind.head().contains("video/mp2t"));
        // Still the first connection to the server.
        QCOMPARE(server.requests().size(), 1);
        QCOMPARE(sessionFolders(root), 1);

        proxy.end(url);
        QVERIFY(behind.waitClosed());
        QCOMPARE(sessionFolders(root), 0);
    }

    void holdKeepsTheStreamFromAPause()
    {
        const QByteArray first = channel(0, 2000);
        const QByteArray second = channel(2100, 5000);
        QPointer<QTcpSocket> upstream;
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket);
            socket->write(first);
            upstream = socket;
        });
        QTemporaryDir root;
        StreamProxy proxy;
        proxy.setTiming(patient());
        proxy.setTimeshiftOptions(timeshiftIn(root));
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);

        Player live(url);
        QVERIFY(QTest::qWaitFor([&]() {
            return live.body().size() >= first.size();
        }));
        // Without rewinding nothing is kept until the pause.
        QVERIFY(!proxy.timeshift(url).available);
        QCOMPARE(sessionFolders(root), 0);

        QVERIFY(proxy.hold(url));
        upstream->write(second);
        QVERIFY(QTest::qWaitFor([&]() {
            return proxy.timeshift(url).windowMs >= 2900;
        }));

        // 1 s behind 5000 ms: the mark before 4000 ms is the one at 3100 ms.
        Player behind(StreamProxy::behind(url, 1000));
        const QByteArray expected = second.mid(offsetOf(2100, 3100));
        QVERIFY(QTest::qWaitFor([&]() {
            return behind.body().size() >= expected.size();
        }));
        QCOMPARE(behind.body(), expected);
    }

    void holdNeedsATransportStream()
    {
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket, "video/x-matroska");
            socket->write(QByteArray(20000, 'm'));
        });
        QTemporaryDir root;
        StreamProxy proxy;
        proxy.setTiming(patient());
        proxy.setTimeshiftOptions(timeshiftIn(root));
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);
        proxy.setRewind(url, 60000);

        Player live(url);
        QVERIFY(QTest::qWaitFor([&]() {
            return live.body().size() >= 20000;
        }));
        QVERIFY(proxy.timeshift(url).known);
        QVERIFY(!proxy.timeshift(url).available);
        QVERIFY(!proxy.hold(url));
        QCOMPARE(sessionFolders(root), 0);
    }

    void playerBehindLiveFinishesWhenTheStreamEnds()
    {
        const QByteArray data = channel(0, 3000);
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int number) {
            if (number > 0) {
                // The channel is gone for good.
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
                socket->disconnectFromHost();

                return;
            }

            ok(socket);
            socket->write(data);
        });
        QTemporaryDir root;
        StreamProxy proxy;
        proxy.setTiming(fast());
        proxy.setTimeshiftOptions(timeshiftIn(root));
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);
        proxy.setRewind(url, 60000);

        Player live(url);
        QVERIFY(QTest::qWaitFor([&]() {
            return proxy.timeshift(url).windowMs >= 3000;
        }));

        Player behind(StreamProxy::behind(url, 2000));
        // The server stalls, the relay gives up, and the live player is closed.
        QVERIFY(live.waitClosed());
        // The player behind live still gets everything up to the end, then the connection closes.
        QVERIFY(behind.waitClosed());
        QCOMPARE(behind.body(), data.mid(offsetOf(0, 1000)));
    }

    void reportsTheLiveBufferWhileItFills()
    {
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket);
            socket->write(channel(0, 2000));
        });
        StreamProxy proxy;
        proxy.setTiming(patient());
        const QUrl url = proxy.relay(server.url(), {}, {}, 5);
        QCOMPARE(proxy.liveBuffer(url).targetMs, 0);

        auto live = std::make_unique<Player>(url);
        QVERIFY(QTest::qWaitFor([&]() {
            return proxy.liveBuffer(url).bufferedMs >= 2000;
        }));
        const StreamProxy::BufferState state = proxy.liveBuffer(url);
        QVERIFY(state.filling);
        QCOMPARE(state.targetMs, 5000);
        // Held back until 5 s are there.
        QVERIFY(live->body().isEmpty());

        live->close();
        QVERIFY(live->waitClosed());
        live.reset();
        QVERIFY(QTest::qWaitFor([&]() {
            return proxy.liveBuffer(url).targetMs == 0;
        }));
    }

    void sessionWithoutRewindEndsWithThePlayer()
    {
        FakeServer server([&](QTcpSocket *socket, const QByteArray &, int) {
            ok(socket);
            socket->write(channel(0, 1000));
        });
        StreamProxy proxy;
        proxy.setTiming(patient());
        const QUrl url = proxy.relay(server.url(), {}, {}, 0);

        {
            Player live(url);
            QVERIFY(QTest::qWaitFor([&]() {
                return live.body().size() > 0;
            }));
            live.close();
            QVERIFY(live.waitClosed());
        }

        // A new connection to the same address starts over at the server.
        Player again(url);
        QVERIFY(QTest::qWaitFor([&]() {
            return server.requests().size() == 2;
        }));
    }
};

QTEST_GUILESS_MAIN(StreamProxyTest)

#include "test_streamproxy.moc"
