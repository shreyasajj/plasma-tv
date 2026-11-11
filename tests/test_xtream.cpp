// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fakeserver.h"
#include "sources/headers.h"
#include "sources/xtream.h"
#include "sources/xtreamsource.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

namespace
{
QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(FLIMMER_TEST_DATA "/xtream/") + name);

    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Missing test data");
    }

    return file.readAll();
}

const QString s_password = QStringLiteral("p@ss&w=rd");
}

class XtreamTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void baseUrls()
    {
        QCOMPARE(Xtream::baseUrl(QStringLiteral("panel.example:8080")).toString(),
                 QStringLiteral("http://panel.example:8080/"));
        QCOMPARE(Xtream::baseUrl(QStringLiteral("https://panel.example/iptv")).toString(),
                 QStringLiteral("https://panel.example/iptv/"));
        // The API address pasted as a whole.
        QCOMPARE(
            Xtream::baseUrl(QStringLiteral("http://panel.example:8080/player_api.php?username=a"))
                .toString(),
            QStringLiteral("http://panel.example:8080/"));
        QVERIFY(!Xtream::baseUrl(QString()).isValid());
    }

    void addressesKeepThePassword()
    {
        const QUrl base(QStringLiteral("http://panel.example:8080/"));
        const QUrl api = Xtream::apiUrl(base,
                                        QStringLiteral("viewer"),
                                        s_password,
                                        QStringLiteral("get_live_streams"));
        QCOMPARE(api.path(), QStringLiteral("/player_api.php"));
        const QUrlQuery query(api);
        QCOMPARE(query.queryItemValue(QStringLiteral("password"), QUrl::FullyDecoded), s_password);
        QCOMPARE(query.queryItemValue(QStringLiteral("action")),
                 QStringLiteral("get_live_streams"));

        const QUrl guide = Xtream::guideUrl(base, QStringLiteral("viewer"), s_password);
        QCOMPARE(guide.path(), QStringLiteral("/xmltv.php"));
        QCOMPARE(QUrlQuery(guide).queryItemValue(QStringLiteral("password"), QUrl::FullyDecoded),
                 s_password);
    }

    void streamUrls()
    {
        const QUrl base(QStringLiteral("http://panel.example:8080/"));
        const QUrl ts = Xtream::streamUrl(base,
                                          QStringLiteral("viewer"),
                                          s_password,
                                          QStringLiteral("101"),
                                          {QStringLiteral("m3u8"), QStringLiteral("ts")});
        QCOMPARE(ts.path(QUrl::FullyDecoded),
                 QStringLiteral("/live/viewer/") + s_password + QStringLiteral("/101.ts"));

        // Only when the account can't have MPEG-TS.
        const QUrl hls = Xtream::streamUrl(base,
                                           QStringLiteral("viewer"),
                                           QStringLiteral("pw"),
                                           QStringLiteral("101"),
                                           {QStringLiteral("m3u8")});
        QCOMPARE(hls.path(), QStringLiteral("/live/viewer/pw/101.m3u8"));
        QCOMPARE(Xtream::streamUrl(base,
                                   QStringLiteral("a"),
                                   QStringLiteral("b"),
                                   QStringLiteral("1"),
                                   {})
                     .path(),
                 QStringLiteral("/live/a/b/1.ts"));
    }

    void parsesLogin()
    {
        Xtream::Account account;
        QVERIFY(Xtream::parseLogin(fixture(QStringLiteral("login.json")), &account));
        QVERIFY(account.auth);
        QCOMPARE(account.status, QStringLiteral("Active"));
        QCOMPARE(account.expires, 4102444800);
        QCOMPARE(account.maxConnections, 1);
        QCOMPARE(account.activeConnections, 0);
        QVERIFY(account.formats.contains(QStringLiteral("ts")));
        QVERIFY(Xtream::loginError(account, 1767000000).isEmpty());
        QVERIFY(!Xtream::inUse(account));

        QVERIFY(!Xtream::parseLogin("<html>nginx</html>", &account));
    }

    void loginErrors()
    {
        Xtream::Account account;
        QVERIFY(Xtream::parseLogin(R"({"user_info":{"auth":0}})", &account));
        QCOMPARE(Xtream::loginError(account, 0), QStringLiteral("Wrong username or password"));

        QVERIFY(
            Xtream::parseLogin(R"({"user_info":{"auth":"1","status":"Expired","exp_date":"1000"}})",
                               &account));
        QVERIFY(
            Xtream::loginError(account, 2000).startsWith(QLatin1String("The account expired on")));

        QVERIFY(Xtream::parseLogin(R"({"user_info":{"auth":1,"status":"Banned","exp_date":null}})",
                                   &account));
        QCOMPARE(Xtream::loginError(account, 2000), QStringLiteral("The account is banned"));

        QVERIFY(Xtream::parseLogin(
            R"({"user_info":{"auth":1,"status":"Active","max_connections":"2","active_cons":2}})",
            &account));
        QVERIFY(Xtream::inUse(account));
    }

    void parsesStreams()
    {
        Xtream::Streams streams;
        QVERIFY(Xtream::parseStreams(fixture(QStringLiteral("live_streams.json")), &streams));

        // The entry without a stream_id is skipped; numbers come as numbers or strings.
        QCOMPARE(streams.channels.size(), 2);
        QCOMPARE(streams.channels.at(0).uuid, QStringLiteral("101"));
        QCOMPARE(streams.channels.at(0).number, 1);
        QCOMPARE(streams.channels.at(0).icon, QStringLiteral("http://logos.example/bbc1.png"));
        QCOMPARE(streams.channels.at(1).uuid, QStringLiteral("102"));
        QCOMPARE(streams.channels.at(1).number, 2);
        QCOMPARE(streams.guideMap.byId.value(QStringLiteral("bbc1.uk")),
                 QStringList{QStringLiteral("101")});
        QCOMPARE(streams.guideMap.byName.value(QStringLiteral("das erste")),
                 QStringList{QStringLiteral("102")});

        QVERIFY(!Xtream::parseStreams(R"({"user_info":{"auth":0}})", &streams));
    }

    void loadsChannelsAndGuide()
    {
        FakeServer server([](const FakeServer::Request &request) -> FakeServer::Reply {
            if (request.url.path() == QLatin1String("/xmltv.php")) {
                return {.body = fixture(QStringLiteral("xmltv.xml")), .type = "application/xml"};
            }

            if (request.query(QStringLiteral("action")) == QLatin1String("get_live_streams")) {
                return {.body = fixture(QStringLiteral("live_streams.json"))};
            }

            return {.body = fixture(QStringLiteral("login.json"))};
        });

        SourceConfig config;
        config.id = QStringLiteral("src-1");
        config.type = SourceConfig::Xtream;
        config.fields.insert(QStringLiteral("server"), server.address());
        config.fields.insert(QStringLiteral("username"), QStringLiteral("viewer"));
        config.fields.insert(QStringLiteral("userAgent"), QStringLiteral("VLC/3.0"));
        config.secrets.insert(QStringLiteral("password"), s_password);
        XtreamSource source(config);

        QSignalSpy channels(&source, &Source::channelsReady);
        QSignalSpy guide(&source, &Source::guideReady);
        QSignalSpy failed(&source, &Source::failed);
        source.refresh(3650); // the test guide is in 2030

        QVERIFY(guide.wait(5000));
        QVERIFY(failed.isEmpty());
        QCOMPARE(channels.count(), 1);
        QCOMPARE(channels.first().first().value<QVector<Channel>>().size(), 2);

        // BBC One is matched by its epg_channel_id, Das Erste (without one) by its name.
        QHash<QString, QString> titles;

        for (const EpgEvent &event : guide.first().first().value<QVector<EpgEvent>>()) {
            titles.insert(event.channel, event.title);
        }

        QCOMPARE(titles.size(), 2);
        QCOMPARE(titles.value(QStringLiteral("101")), QStringLiteral("News"));
        QCOMPARE(titles.value(QStringLiteral("102")), QStringLiteral("Tagesschau"));
        QCOMPARE(server.requests.at(0).query(QStringLiteral("password")), s_password);
        QVERIFY(server.requests.at(0).hasHeader("User-Agent: VLC/3.0"));

        // The stream address holds the password, so it must be relayed.
        QSignalSpy stream(&source, &Source::streamReady);
        source.requestStream(QStringLiteral("101"));
        QCOMPARE(stream.count(), 1);
        const auto target = stream.first().at(1).value<Source::StreamTarget>();
        QVERIFY(target.needsRelay());
        QVERIFY(target.url.path().endsWith(QLatin1String("/101.ts")));
        QCOMPARE(target.headers, HttpHeaders::parse(QStringLiteral("VLC/3.0"), {}));
    }

    void errorsKeepThePasswordToThemselves()
    {
        FakeServer server([](const FakeServer::Request &) -> FakeServer::Reply {
            return {.status = 503};
        });

        SourceConfig config;
        config.type = SourceConfig::Xtream;
        config.fields.insert(QStringLiteral("server"), server.address());
        config.fields.insert(QStringLiteral("username"), QStringLiteral("viewer"));
        config.secrets.insert(QStringLiteral("password"), QStringLiteral("hunter2"));
        XtreamSource source(config);

        // Qt's own message would hold the whole address.
        QSignalSpy failed(&source, &Source::failed);
        source.refresh(1);
        QVERIFY(failed.wait(5000));
        QVERIFY(!failed.first().first().toString().contains(QLatin1String("hunter2")));
        QVERIFY(failed.first().first().toString().contains(QLatin1String("503")));
    }

    void reportsWrongLogin()
    {
        FakeServer server([](const FakeServer::Request &) -> FakeServer::Reply {
            return {.body = R"({"user_info":{"auth":0}})"};
        });

        SourceConfig config;
        config.type = SourceConfig::Xtream;
        config.fields.insert(QStringLiteral("server"), server.address());
        config.fields.insert(QStringLiteral("username"), QStringLiteral("viewer"));
        XtreamSource source(config);

        QSignalSpy failed(&source, &Source::failed);
        source.refresh(1);
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Wrong username or password"));

        QSignalSpy tested(&source, &Source::testFinished);
        source.test();
        QVERIFY(tested.wait(5000));
        QCOMPARE(tested.first().at(0).toBool(), false);
    }

    void explainsAccountInUse()
    {
        FakeServer server([](const FakeServer::Request &) -> FakeServer::Reply {
            return {
                .body =
                    R"({"user_info":{"auth":1,"status":"Active","max_connections":"1","active_cons":"1"}})"};
        });

        SourceConfig config;
        config.type = SourceConfig::Xtream;
        config.fields.insert(QStringLiteral("server"), server.address());
        config.fields.insert(QStringLiteral("username"), QStringLiteral("viewer"));
        XtreamSource source(config);

        QSignalSpy failed(&source, &Source::streamFailed);
        source.explainStreamError(QStringLiteral("101"));
        QVERIFY(failed.wait(5000));
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("101"));
        QCOMPARE(failed.first().at(1).toString(),
                 QStringLiteral("Your account is already in use on another device"));
    }
};

QTEST_GUILESS_MAIN(XtreamTest)

#include "test_xtream.moc"
