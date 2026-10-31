// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sources/sourceconfig.h"

#include <QTest>

class SourceConfigTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void roundTripsTvheadend()
    {
        ServerConfig server;
        server.host = QStringLiteral("tv.local");
        server.port = 9982;
        server.https = true;
        server.username = QStringLiteral("me");
        server.password = QStringLiteral("secret");

        const SourceConfig config = SourceConfig::fromServer(server);
        QCOMPARE(config.type, SourceConfig::Tvheadend);
        QCOMPARE(config.name, QStringLiteral("tv.local"));
        QCOMPARE(config.secret(QStringLiteral("password")), QStringLiteral("secret"));
        QVERIFY(!config.fields.contains(QStringLiteral("password")));
        QCOMPARE(config.serverConfig(), server);
        QVERIFY(config.isValid());
    }

    void roundTripsM3u()
    {
        ServerConfig server;
        server.backend = ServerConfig::Backend::M3u;
        server.playlistUrl = QStringLiteral("http://iptv.local/playlist.m3u");
        server.xmltvUrl = QStringLiteral("http://iptv.local/epg.xml");

        const SourceConfig config = SourceConfig::fromServer(server);
        QCOMPARE(config.type, SourceConfig::M3u);
        const ServerConfig back = config.serverConfig();
        QCOMPARE(back.backend, ServerConfig::Backend::M3u);
        QCOMPARE(back.playlistUrl, server.playlistUrl);
        QCOMPARE(back.xmltvUrl, server.xmltvUrl);
    }

    void appliesValues()
    {
        SourceConfig config;
        config.type = SourceConfig::Stalker;
        config.apply({{QStringLiteral("name"), QStringLiteral(" Portal ")},
                      {QStringLiteral("portal"), QStringLiteral(" http://portal.local/c/ ")},
                      {QStringLiteral("mac"), QStringLiteral("00:1A:79:00:00:01")},
                      {QStringLiteral("stbModel"), QStringLiteral("MAG254")},
                      {QStringLiteral("enabled"), false}});

        QCOMPARE(config.name, QStringLiteral("Portal"));
        QCOMPARE(config.field(QStringLiteral("portal")), QStringLiteral("http://portal.local/c/"));
        // The MAC address is a secret, never a plain field.
        QCOMPARE(config.secret(QStringLiteral("mac")), QStringLiteral("00:1A:79:00:00:01"));
        QVERIFY(!config.fields.contains(QStringLiteral("mac")));
        QVERIFY(!config.enabled);
        QVERIFY(config.isValid());

        // An emptied secret is removed.
        config.apply({{QStringLiteral("mac"), QString()}});
        QVERIFY(!config.secrets.contains(QStringLiteral("mac")));
        QVERIFY(!config.isValid());
    }

    void noPasswordWithoutUser()
    {
        SourceConfig config;
        config.type = SourceConfig::Xtream;
        config.apply({{QStringLiteral("server"), QStringLiteral("http://panel.local:8080")},
                      {QStringLiteral("username"), QString()},
                      {QStringLiteral("password"), QStringLiteral("x")}});

        QVERIFY(config.secrets.isEmpty());
    }

    void newIdsLookRight()
    {
        const QString id = SourceConfig::newId();
        QVERIFY(id.startsWith(QLatin1String("src-")));
        QCOMPARE(id.size(), 8);
    }
};

QTEST_GUILESS_MAIN(SourceConfigTest)

#include "test_sourceconfig.moc"
