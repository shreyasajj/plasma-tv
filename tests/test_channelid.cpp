// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sources/channelid.h"

#include <QTest>

namespace
{
Channel channel(const QString &uuid, int number)
{
    Channel c;
    c.uuid = uuid;
    c.number = number;
    c.name = uuid;

    return c;
}

QList<int> numbers(const QVector<Channel> &channels)
{
    QList<int> result;

    for (const Channel &c : channels) {
        result.append(c.number);
    }

    return result;
}
}

class ChannelIdTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void prefixes()
    {
        const QString id = ChannelId::make(QStringLiteral("src-3f2a"), QStringLiteral("1234"));
        QCOMPARE(id, QStringLiteral("src-3f2a:1234"));
        QCOMPARE(ChannelId::sourceOf(id), QStringLiteral("src-3f2a"));
        QCOMPARE(ChannelId::ownOf(id), QStringLiteral("1234"));

        // Own IDs may contain colons themselves (M3U tvg-ids, URLs).
        const QString odd = ChannelId::make(QStringLiteral("src-1"), QStringLiteral("a:b:c"));
        QCOMPARE(ChannelId::ownOf(odd), QStringLiteral("a:b:c"));
        QCOMPARE(ChannelId::sourceOf(odd), QStringLiteral("src-1"));
    }

    void eventIds()
    {
        const qint64 id = ChannelId::makeEvent(5, 123456789);
        QCOMPARE(ChannelId::sourceIndexOf(id), 5);
        QCOMPARE(ChannelId::ownEventOf(id), 123456789);
        QVERIFY(ChannelId::makeEvent(0, 7) != ChannelId::makeEvent(1, 7));
        QCOMPARE(ChannelId::makeEvent(0, 7), 7);
    }

    void prefixesLists()
    {
        EpgEvent event;
        event.id = 9;
        event.channel = QStringLiteral("a");
        const QVector<EpgEvent> events = ChannelId::prefixed(QStringLiteral("s"), 2, {event});
        QCOMPARE(events.first().channel, QStringLiteral("s:a"));
        QCOMPARE(ChannelId::ownEventOf(events.first().id), 9);
        QCOMPARE(ChannelId::sourceIndexOf(events.first().id), 2);

        const QVector<Channel> channels =
            ChannelId::prefixed(QStringLiteral("s"), {channel(QStringLiteral("a"), 1)});
        QCOMPARE(channels.first().uuid, QStringLiteral("s:a"));
    }

    void oneSourceStaysAsItIs()
    {
        // Playlist order and duplicate numbers are kept with a single source.
        const QVector<Channel> one = {channel(QStringLiteral("b"), 2),
                                      channel(QStringLiteral("a"), 2),
                                      channel(QStringLiteral("c"), 0)};
        QCOMPARE(numbers(ChannelId::merge({one})), (QList<int>{2, 2, 0}));
    }

    void renumbersClashes()
    {
        const QVector<Channel> first = {channel(QStringLiteral("1:a"), 1),
                                        channel(QStringLiteral("1:b"), 2),
                                        channel(QStringLiteral("1:c"), 0)};
        const QVector<Channel> second = {channel(QStringLiteral("2:a"), 2),
                                         channel(QStringLiteral("2:b"), 3),
                                         channel(QStringLiteral("2:c"), 2)};

        const QVector<Channel> merged = ChannelId::merge({first, second});

        // 2 is taken by the first source: both of the second source's 2s get new numbers after
        // the highest (3); its own 3 is free and stays.
        QCOMPARE(merged.size(), 6);
        QCOMPARE(numbers(merged), (QList<int>{1, 2, 3, 4, 5, 0}));
        QCOMPARE(merged.at(2).uuid, QStringLiteral("2:b"));
        QCOMPARE(merged.at(3).uuid, QStringLiteral("2:a"));
        QCOMPARE(merged.at(4).uuid, QStringLiteral("2:c"));
        QCOMPARE(merged.last().uuid, QStringLiteral("1:c"));
    }
};

QTEST_GUILESS_MAIN(ChannelIdTest)

#include "test_channelid.moc"
