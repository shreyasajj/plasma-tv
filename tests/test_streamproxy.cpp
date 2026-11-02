// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streamproxy.h"

#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
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
            m_socket.write("GET " + url.path().toLatin1() + " HTTP/1.1\r\nHost: x\r\n\r\n");
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

StreamProxy::Timing fast()
{
    StreamProxy::Timing timing;
    timing.stallMs = 400;
    timing.retryDelaysMs = {20, 20, 20};

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
};

QTEST_GUILESS_MAIN(StreamProxyTest)

#include "test_streamproxy.moc"
