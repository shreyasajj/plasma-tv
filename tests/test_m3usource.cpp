// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fakeserver.h"
#include "sources/headers.h"
#include "sources/m3usource.h"

#include <QSignalSpy>
#include <QTest>

class M3uSourceTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesHeaders()
    {
        const Headers headers = HttpHeaders::parse(
            QStringLiteral(" VLC/3.0 "),
            QStringLiteral("X-Token: abc | Referer: http://a/b\nbroken line\n : x"));
        QCOMPARE(headers.size(), 3);
        QCOMPARE(headers.at(0), (QPair<QByteArray, QByteArray>("User-Agent", "VLC/3.0")));
        QCOMPARE(headers.at(1), (QPair<QByteArray, QByteArray>("X-Token", "abc")));
        // A value may contain colons.
        QCOMPARE(headers.at(2), (QPair<QByteArray, QByteArray>("Referer", "http://a/b")));
        QVERIFY(HttpHeaders::parse({}, {}).isEmpty());
    }

    void sendsHeaders()
    {
        FakeServer server([](const FakeServer::Request &) -> FakeServer::Reply {
            return {.body = "#EXTM3U\n#EXTINF:-1 tvg-id=\"one\",One\nhttp://streams.example/one.ts\n",
                    .type = "audio/x-mpegurl"};
        });

        SourceConfig config;
        config.id = QStringLiteral("src-1");
        config.type = SourceConfig::M3u;
        config.fields.insert(QStringLiteral("playlistUrl"),
                             server.address() + QStringLiteral("list.m3u"));
        config.fields.insert(QStringLiteral("userAgent"), QStringLiteral("VLC/3.0"));
        config.fields.insert(QStringLiteral("headers"), QStringLiteral("X-Token: abc"));
        M3uSource source(config);

        QSignalSpy channels(&source, &Source::channelsReady);
        source.refresh(1);
        QVERIFY(channels.wait(5000));

        // The playlist is asked for with the headers …
        QVERIFY(server.requests.at(0).hasHeader("User-Agent: VLC/3.0"));
        QVERIFY(server.requests.at(0).hasHeader("X-Token: abc"));

        // … and the stream gets them from the relay, as the player can't send them.
        QSignalSpy stream(&source, &Source::streamReady);
        source.requestStream(QStringLiteral("one"));
        QCOMPARE(stream.count(), 1);
        const auto target = stream.first().at(1).value<Source::StreamTarget>();
        QCOMPARE(target.headers.size(), 2);
        QVERIFY(target.needsRelay());
    }
};

QTEST_GUILESS_MAIN(M3uSourceTest)

#include "test_m3usource.moc"
