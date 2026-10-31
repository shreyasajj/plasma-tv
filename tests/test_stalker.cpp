// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fakeserver.h"
#include "sources/stalker.h"
#include "sources/stalkersource.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

namespace
{
QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(FLIMMER_TEST_DATA "/stalker/") + name);

    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Missing test data");
    }

    return file.readAll();
}

QByteArray header(const Headers &headers, const QByteArray &name)
{
    for (const auto &[key, value] : headers) {
        if (key == name) {
            return value;
        }
    }

    return {};
}

// A portal on /stalker_portal/server/load.php only; tokens are numbered so a renewed one shows.
struct Portal {
    int tokens = 0;
    bool expireOnce = false;
    QByteArray authReply = R"({"js":true})";

    FakeServer::Reply answer(const FakeServer::Request &request)
    {
        if (request.url.path() != QLatin1String("/stalker_portal/server/load.php")) {
            return {.status = 404, .body = "Not found", .type = "text/html"};
        }

        const QString action = request.query(QStringLiteral("action"));

        if (action == QLatin1String("handshake")) {
            ++tokens;

            return {.body = fixture(QStringLiteral("handshake.json"))
                                .replace("TOKEN-1", "TOKEN-" + QByteArray::number(tokens))};
        }

        // Everything else needs the current token.
        if (!request.hasHeader("Authorization: Bearer TOKEN-" + QByteArray::number(tokens))) {
            return {.body = "Authorization failed.", .type = "text/plain"};
        }

        if (action == QLatin1String("get_profile")) {
            return {.body = fixture(QStringLiteral("profile.json"))};
        }

        if (action == QLatin1String("do_auth")) {
            return {.body = authReply};
        }

        if (action == QLatin1String("get_all_channels")) {
            return {.body = fixture(QStringLiteral("channels.json"))};
        }

        if (action == QLatin1String("get_epg_info")) {
            return {.body = fixture(QStringLiteral("epg.json"))};
        }

        if (action == QLatin1String("create_link")) {
            if (expireOnce) {
                expireOnce = false;

                return {.body = "Authorization failed.", .type = "text/plain"};
            }

            return {.body = fixture(QStringLiteral("link.json"))};
        }

        return {.status = 400, .body = "{}"};
    }
};

SourceConfig config(const QString &portal)
{
    SourceConfig config;
    config.id = QStringLiteral("src-s");
    config.type = SourceConfig::Stalker;
    config.fields.insert(QStringLiteral("portal"), portal + QStringLiteral("stalker_portal/c/"));
    config.secrets.insert(QStringLiteral("mac"), QStringLiteral("00:1a:79:00:00:01"));
    config.secrets.insert(QStringLiteral("serial"), QStringLiteral("SN123"));

    return config;
}
}

class StalkerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizesMacAddresses()
    {
        QCOMPARE(Stalker::normalizeMac(QStringLiteral("00:1a:79:aa:bb:cc")),
                 QStringLiteral("00:1A:79:AA:BB:CC"));
        QCOMPARE(Stalker::normalizeMac(QStringLiteral(" 001A79AABBCC ")),
                 QStringLiteral("00:1A:79:AA:BB:CC"));
        QCOMPARE(Stalker::normalizeMac(QStringLiteral("00-1A-79-AA-BB-CC")),
                 QStringLiteral("00:1A:79:AA:BB:CC"));
        QVERIFY(Stalker::normalizeMac(QStringLiteral("00:1A:79:AA:BB")).isEmpty());
        QVERIFY(Stalker::normalizeMac(QStringLiteral("00:1A:79:AA:BB:ZZ")).isEmpty());
    }

    void findsPortalRoot()
    {
        QCOMPARE(Stalker::portalRoot(QStringLiteral("http://portal.example/c/")).toString(),
                 QStringLiteral("http://portal.example/"));
        QCOMPARE(
            Stalker::portalRoot(QStringLiteral("portal.example:8080/stalker_portal/c")).toString(),
            QStringLiteral("http://portal.example:8080/stalker_portal/"));
        QCOMPARE(Stalker::portalRoot(QStringLiteral("http://portal.example/portal.php")).toString(),
                 QStringLiteral("http://portal.example/"));
        QCOMPARE(Stalker::portalRoot(
                     QStringLiteral("http://portal.example/stalker_portal/server/load.php"))
                     .toString(),
                 QStringLiteral("http://portal.example/stalker_portal/"));
        // A path merely ending in "c" is kept.
        QCOMPARE(Stalker::portalRoot(QStringLiteral("http://portal.example/abc")).toString(),
                 QStringLiteral("http://portal.example/abc/"));

        const QList<QUrl> endpoints =
            Stalker::endpoints(QUrl(QStringLiteral("http://portal.example/")));
        QCOMPARE(endpoints.size(), 3);
        QCOMPARE(endpoints.at(0).path(), QStringLiteral("/portal.php"));
        QCOMPARE(endpoints.at(1).path(), QStringLiteral("/server/load.php"));
        QCOMPARE(endpoints.at(2).path(), QStringLiteral("/stalker_portal/server/load.php"));
    }

    void buildsHeaders()
    {
        Stalker::Device device;
        device.mac = QStringLiteral("00:1A:79:00:00:01");
        device.model = QStringLiteral("MAG254");
        device.timezone = QStringLiteral("Europe/Berlin");
        const QUrl root(QStringLiteral("http://portal.example/"));

        const Headers headers = Stalker::headers(device, root, QStringLiteral("T"));
        QVERIFY(header(headers, "User-Agent").contains("MAG254 stbapp"));
        QCOMPARE(header(headers, "X-User-Agent"), QByteArray("Model: MAG254; Link: WiFi"));
        QCOMPARE(
            header(headers, "Cookie"),
            QByteArray("mac=00%3A1A%3A79%3A00%3A00%3A01; stb_lang=en; timezone=Europe%2FBerlin"));
        QCOMPARE(header(headers, "Referer"), QByteArray("http://portal.example/c/"));
        QCOMPARE(header(headers, "Authorization"), QByteArray("Bearer T"));

        // Before the handshake, and on the stream: no token.
        QVERIFY(header(Stalker::headers(device, root), "Authorization").isEmpty());
        QVERIFY(header(Stalker::streamHeaders(device, root), "Authorization").isEmpty());
        QVERIFY(!header(Stalker::streamHeaders(device, root), "Cookie").isEmpty());
    }

    void leavesOutEmptyFields()
    {
        Stalker::Device device;
        device.serial = QStringLiteral("SN123");
        const QUrlQuery query(
            Stalker::profileUrl(QUrl(QStringLiteral("http://p/portal.php")), device));
        QCOMPARE(query.queryItemValue(QStringLiteral("action")), QStringLiteral("get_profile"));
        QCOMPARE(query.queryItemValue(QStringLiteral("sn")), QStringLiteral("SN123"));
        QCOMPARE(query.queryItemValue(QStringLiteral("stb_type")), QStringLiteral("MAG250"));
        QVERIFY(!query.hasQueryItem(QStringLiteral("device_id")));
        QVERIFY(!query.hasQueryItem(QStringLiteral("device_id2")));
        QVERIFY(!query.hasQueryItem(QStringLiteral("signature")));
        QCOMPARE(query.queryItemValue(QStringLiteral("JsHttpRequest")), QStringLiteral("1-xml"));
    }

    void parsesReplies()
    {
        QCOMPARE(Stalker::parseToken(fixture(QStringLiteral("handshake.json"))),
                 QStringLiteral("TOKEN-1"));
        QVERIFY(Stalker::parseToken("<html>").isEmpty());
        QVERIFY(Stalker::parseProfile(fixture(QStringLiteral("profile.json"))));
        QVERIFY(Stalker::parseAuth(R"({"js":true})"));
        QVERIFY(!Stalker::parseAuth(R"({"js":false})"));
        QVERIFY(Stalker::isAuthFailure(200, "Authorization failed."));
        QVERIFY(Stalker::isAuthFailure(401, {}));
        QVERIFY(!Stalker::isAuthFailure(200, fixture(QStringLiteral("profile.json"))));

        QVector<Channel> channels;
        QVERIFY(
            Stalker::parseChannels(fixture(QStringLiteral("channels.json")),
                                   QUrl(QStringLiteral("http://portal.example/stalker_portal/")),
                                   &channels));
        // The channel without a cmd can't be played and is left out.
        QCOMPARE(channels.size(), 2);
        QCOMPARE(channels.at(0).uuid, QStringLiteral("1"));
        QCOMPARE(channels.at(0).number, 1);
        QCOMPARE(channels.at(0).stream, QStringLiteral("ffmpeg http://localhost/ch/1_"));
        QCOMPARE(channels.at(0).icon,
                 QStringLiteral("http://portal.example/stalker_portal/misc/logos/320/1.png"));
        QCOMPARE(channels.at(1).icon, QStringLiteral("http://logos.example/daserste.png"));

        QVector<EpgEvent> events;
        QVERIFY(Stalker::parseGuide(fixture(QStringLiteral("epg.json")), &events));
        // The event without length is left out; timestamps come as numbers or strings.
        QCOMPARE(events.size(), 2);
        std::sort(events.begin(), events.end(), [](const EpgEvent &a, const EpgEvent &b) {
            return a.id < b.id;
        });
        QCOMPARE(events.at(0).channel, QStringLiteral("1"));
        QCOMPARE(events.at(0).title, QStringLiteral("News"));
        QCOMPARE(events.at(0).description, QStringLiteral("The headlines"));
        QCOMPARE(events.at(1).stop, 1893499200);
        QCOMPARE(events.at(1).genre, 0x10);

        QCOMPARE(Stalker::parseLink(fixture(QStringLiteral("link.json"))).toString(),
                 QStringLiteral("http://streams.example/live/play/abc.ts?play_token=XYZ"));
        QCOMPARE(Stalker::parseLink("{\"js\":{\"cmd\":\"http://a/b.ts\"}}").toString(),
                 QStringLiteral("http://a/b.ts"));
        QVERIFY(!Stalker::parseLink("{\"js\":{\"cmd\":\"\"}}").isValid());
    }

    void loadsFromPortal()
    {
        Portal portal;
        FakeServer server([&portal](const FakeServer::Request &request) {
            return portal.answer(request);
        });
        StalkerSource source(config(server.address()));

        QSignalSpy channels(&source, &Source::channelsReady);
        QSignalSpy guide(&source, &Source::guideReady);
        QSignalSpy failed(&source, &Source::failed);
        source.refresh(1);

        QVERIFY(guide.wait(5000));
        QVERIFY(failed.isEmpty());
        QCOMPARE(channels.first().first().value<QVector<Channel>>().size(), 2);
        QCOMPARE(guide.first().first().value<QVector<EpgEvent>>().size(), 2);

        // The portal's path was searched: two misses, then the handshake that worked.
        QCOMPARE(server.requests.at(0).url.path(), QStringLiteral("/stalker_portal/portal.php"));
        QCOMPARE(server.requests.at(1).url.path(),
                 QStringLiteral("/stalker_portal/server/load.php"));
        QVERIFY(server.requests.at(1).hasHeader("Cookie: mac=00%3A1A%3A79%3A00%3A00%3A01"));
        QVERIFY(server.requests.at(1).hasHeader("X-User-Agent: Model: MAG250; Link: WiFi"));

        // The profile carries the serial number, and only the fields that are set.
        const auto profile =
            std::find_if(server.requests.cbegin(), server.requests.cend(), [](const auto &r) {
                return r.query(QStringLiteral("action")) == QLatin1String("get_profile");
            });
        QVERIFY(profile != server.requests.cend());
        QCOMPARE(profile->query(QStringLiteral("sn")), QStringLiteral("SN123"));
        QVERIFY(!QUrlQuery(profile->url).hasQueryItem(QStringLiteral("signature")));
        // No username: no do_auth.
        QVERIFY(std::none_of(server.requests.cbegin(), server.requests.cend(), [](const auto &r) {
            return r.query(QStringLiteral("action")) == QLatin1String("do_auth");
        }));

        // A stream link, with the box's headers for the relay.
        QSignalSpy stream(&source, &Source::streamReady);
        source.requestStream(QStringLiteral("1"));
        QVERIFY(stream.wait(5000));
        const auto target = stream.first().at(1).value<Source::StreamTarget>();
        QCOMPARE(target.url.toString(),
                 QStringLiteral("http://streams.example/live/play/abc.ts?play_token=XYZ"));
        QVERIFY(target.needsRelay());
        QVERIFY(!header(target.headers, "User-Agent").isEmpty());
        const auto link =
            std::find_if(server.requests.cbegin(), server.requests.cend(), [](const auto &r) {
                return r.query(QStringLiteral("action")) == QLatin1String("create_link");
            });
        QCOMPARE(link->query(QStringLiteral("cmd")),
                 QStringLiteral("ffmpeg http://localhost/ch/1_"));
    }

    void renewsExpiredToken()
    {
        Portal portal;
        FakeServer server([&portal](const FakeServer::Request &request) {
            return portal.answer(request);
        });
        StalkerSource source(config(server.address()));
        source.restoreChannels({Channel{QStringLiteral("1"),
                                        1,
                                        QStringLiteral("BBC"),
                                        {},
                                        QStringLiteral("ffmpeg http://localhost/ch/1_")}});

        QSignalSpy stream(&source, &Source::streamReady);
        QSignalSpy streamFailed(&source, &Source::streamFailed);
        portal.expireOnce = true;
        source.requestStream(QStringLiteral("1"));

        QVERIFY(stream.wait(5000));
        QVERIFY(streamFailed.isEmpty());
        // A second handshake gave a new token, then create_link worked.
        QCOMPARE(portal.tokens, 2);
    }

    void reportsWrongLogin()
    {
        Portal portal;
        portal.authReply = R"({"js":false})";
        FakeServer server([&portal](const FakeServer::Request &request) {
            return portal.answer(request);
        });
        SourceConfig withLogin = config(server.address());
        withLogin.fields.insert(QStringLiteral("username"), QStringLiteral("viewer"));
        withLogin.secrets.insert(QStringLiteral("password"), QStringLiteral("pw"));
        StalkerSource source(withLogin);

        QSignalSpy tested(&source, &Source::testFinished);
        source.test();
        QVERIFY(tested.wait(5000));
        QCOMPARE(tested.first().at(0).toBool(), false);
        QCOMPARE(tested.first().at(1).toString(), QStringLiteral("Wrong username or password"));
    }

    void needsMacAddress()
    {
        SourceConfig noMac = config(QStringLiteral("http://portal.example/"));
        noMac.secrets.remove(QStringLiteral("mac"));
        StalkerSource source(noMac);

        QSignalSpy failed(&source, &Source::failed);
        source.refresh(1);
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.first().first().toString().contains(QLatin1String("MAC address")));
    }

    void identityHidesTheMacAddress()
    {
        const SourceConfig box = config(QStringLiteral("http://portal.example/"));
        SourceConfig other = box;
        other.secrets.insert(QStringLiteral("mac"), QStringLiteral("00:1a:79:00:00:02"));

        // It ends up in the cache, but still tells two boxes apart.
        const QString identity = StalkerSource(box).identity();
        QVERIFY(!identity.contains(QLatin1String("00:1A:79"), Qt::CaseInsensitive));
        QCOMPARE(StalkerSource(box).identity(), identity);
        QVERIFY(StalkerSource(other).identity() != identity);
    }

    void notAPortal()
    {
        FakeServer server([](const FakeServer::Request &) -> FakeServer::Reply {
            return {.body = "<html>Hello</html>", .type = "text/html"};
        });
        StalkerSource source(config(server.address()));

        QSignalSpy failed(&source, &Source::failed);
        source.refresh(1);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().first().toString(),
                 QStringLiteral("The server answered, but does not look like a Stalker portal"));
        // All three paths were tried.
        QCOMPARE(server.requests.size(), 3);
    }
};

QTEST_GUILESS_MAIN(StalkerTest)

#include "test_stalker.moc"
