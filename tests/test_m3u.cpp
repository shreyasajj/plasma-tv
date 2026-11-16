// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "m3u.h"

#include <QTest>

namespace
{
const QUrl s_source(QStringLiteral( // NOLINT(bugprone-throwing-static-initialization)
    "http://iptv.local:9191/output/m3u"));

class M3uTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    static void stableControlIdsKeepLegacyGuideKeys()
    {
        const auto first = M3u::parse(
            "#EXTM3U\n#EXTINF:-1 tvg-id=\"same\",News\nhttp://host/old-token/1\n"
            "#EXTINF:-1 tvg-id=\"same\",Sports\nhttp://host/old-token/2\n",
            s_source);
        const auto reordered = M3u::parse(
            "#EXTM3U\n#EXTINF:-1 tvg-id=\"same\",Sports\nhttp://host/new-token/2\n"
            "#EXTINF:-1 tvg-id=\"same\",News\nhttp://host/new-token/1\n",
            s_source);
        QCOMPARE(first.channels.at(0).uuid, QStringLiteral("same"));
        QCOMPARE(first.channels.at(1).uuid, QStringLiteral("same#2"));
        QVERIFY(!first.channels.at(0).controlId.isEmpty());
        QCOMPARE(first.channels.at(0).controlId, reordered.channels.at(1).controlId);
        QCOMPARE(first.channels.at(1).controlId, reordered.channels.at(0).controlId);
        const auto ambiguous =
            M3u::parse("#EXTM3U\n#EXTINF:-1,News\nhttp://a/1\n#EXTINF:-1,News\nhttp://a/2\n",
                       s_source);
        QVERIFY(ambiguous.channels.at(0).controlId.isEmpty());
        QVERIFY(ambiguous.channels.at(1).controlId.isEmpty());
    }

    static void looksLikePlaylist()
    {
        QVERIFY(M3u::looksLikePlaylist("#EXTM3U\n"));
        QVERIFY(M3u::looksLikePlaylist("\xEF\xBB\xBF#EXTM3U\n"));
        QVERIFY(M3u::looksLikePlaylist("#EXTINF:-1,One\nhttp://a/1\n"));
        QVERIFY(!M3u::looksLikePlaylist("<html><body>Login</body></html>"));
        QVERIFY(!M3u::looksLikePlaylist(QByteArray()));
    }

    static void parsesAttributes()
    {
        const QByteArray data =
            "#EXTM3U url-tvg=\"http://iptv.local:9191/output/epg\"\n"
            "#EXTINF:-1 tvg-id=\"das.erste\" tvg-chno=\"1\" "
            "tvg-logo=\"logos/1.png\" group-title=\"News, Info\",Das Erste HD\n"
            "http://iptv.local:9191/stream/1\n";

        const M3u::Playlist playlist = M3u::parse(data, s_source);

        QCOMPARE(playlist.guideUrl.toString(), QStringLiteral("http://iptv.local:9191/output/epg"));
        QCOMPARE(playlist.channels.size(), 1);

        const Channel &channel = playlist.channels.first();
        QCOMPARE(channel.uuid, QStringLiteral("das.erste"));
        QCOMPARE(channel.number, 1);
        // The comma inside the quoted group title doesn't end the attributes.
        QCOMPARE(channel.name, QStringLiteral("Das Erste HD"));
        QCOMPARE(channel.icon, QStringLiteral("http://iptv.local:9191/output/logos/1.png"));
        QCOMPARE(channel.stream, QStringLiteral("http://iptv.local:9191/stream/1"));
    }

    static void resolvesRelativeStreams()
    {
        const M3u::Playlist playlist =
            M3u::parse("#EXTM3U\n#EXTINF:-1,One\n../stream/1\n", s_source);

        QCOMPARE(playlist.channels.first().stream,
                 QStringLiteral("http://iptv.local:9191/stream/1"));
    }

    static void numbersUnnumberedPlaylistsInOrder()
    {
        const M3u::Playlist playlist =
            M3u::parse("#EXTM3U\n#EXTINF:-1,B\nhttp://a/b\n#EXTINF:-1,A\nhttp://a/a\n", s_source);

        QCOMPARE(playlist.channels.size(), 2);
        QCOMPARE(playlist.channels.at(0).name, QStringLiteral("B"));
        QCOMPARE(playlist.channels.at(0).number, 1);
        QCOMPARE(playlist.channels.at(1).number, 2);
    }

    static void sortsByNumberWithUnnumberedLast()
    {
        const QByteArray data =
            "#EXTM3U\n"
            "#EXTINF:-1,No number\nhttp://a/x\n"
            "#EXTINF:-1 tvg-chno=\"5\",Five\nhttp://a/5\n"
            "#EXTINF:-1 tvg-chno=\"2.1\",Two\nhttp://a/2\n";

        const M3u::Playlist playlist = M3u::parse(data, s_source);

        QCOMPARE(playlist.channels.size(), 3);
        QCOMPARE(playlist.channels.at(0).name, QStringLiteral("Two"));
        // Decimal numbers keep their major part.
        QCOMPARE(playlist.channels.at(0).number, 2);
        QCOMPARE(playlist.channels.at(1).name, QStringLiteral("Five"));
        QCOMPARE(playlist.channels.at(2).name, QStringLiteral("No number"));
        QCOMPARE(playlist.channels.at(2).number, 0);
    }

    static void keepsUuidsUnique()
    {
        const QByteArray data =
            "#EXTM3U\n"
            "#EXTINF:-1 tvg-id=\"zdf\",ZDF\nhttp://a/1\n"
            "#EXTINF:-1 tvg-id=\"zdf\",ZDF HD\nhttp://a/2\n";

        const M3u::Playlist playlist = M3u::parse(data, s_source);

        QCOMPARE(playlist.channels.at(0).uuid, QStringLiteral("zdf"));
        QCOMPARE(playlist.channels.at(1).uuid, QStringLiteral("zdf#2"));
        // Both share the guide channel.
        QCOMPARE(playlist.guideMap.byId.value(QStringLiteral("zdf")),
                 QStringList({QStringLiteral("zdf"), QStringLiteral("zdf#2")}));
    }

    static void mapsNamesForTheGuide()
    {
        const QByteArray data =
            "#EXTM3U\n"
            "#EXTINF:-1 tvg-name=\"Arte  DE\",ARTE HD\nhttp://a/1\n";

        const M3u::Playlist playlist = M3u::parse(data, s_source);

        // Names are matched case and whitespace insensitive.
        QCOMPARE(playlist.guideMap.byName.value(QStringLiteral("arte hd")),
                 QStringList(QStringLiteral("ARTE HD")));
        QCOMPARE(playlist.guideMap.byName.value(QStringLiteral("arte de")),
                 QStringList(QStringLiteral("ARTE HD")));
        QVERIFY(playlist.guideMap.byId.isEmpty());
    }

    static void ignoresEntriesWithoutMetadata()
    {
        const QByteArray data =
            "#EXTM3U\r\n"
            "http://a/plain\r\n"
            "#EXTVLCOPT:network-caching=1000\r\n"
            "#EXTINF:-1 tvg-name=\"Named\",\r\n"
            "http://a/named\r\n";

        const M3u::Playlist playlist = M3u::parse(data, s_source);

        QCOMPARE(playlist.channels.size(), 1);
        // Without a title the tvg-name is used.
        QCOMPARE(playlist.channels.first().name, QStringLiteral("Named"));
        QCOMPARE(playlist.channels.first().stream, QStringLiteral("http://a/named"));
    }

    static void usesFirstOfSeveralGuides()
    {
        const M3u::Playlist playlist =
            M3u::parse("#EXTM3U x-tvg-url=\"http://a/one.xml, http://a/two.xml\"\n", s_source);

        QCOMPARE(playlist.guideUrl.toString(), QStringLiteral("http://a/one.xml"));
    }
};

}

QTEST_GUILESS_MAIN(M3uTest)

#include "test_m3u.moc"
