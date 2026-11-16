// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "channelidentity.h"

#include <QTest>

// Exercises automation identities without fetching provider playlists.
class ChannelIdentityTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    static void reorderAndRename()
    {
        const auto first =
            ChannelIdentity::forPlaylist({QStringLiteral("a"), QStringLiteral("b")},
                                         {QStringLiteral("News"), QStringLiteral("Sports")});
        const auto second = ChannelIdentity::forPlaylist(
            {QStringLiteral("b"), QStringLiteral("a")},
            {QStringLiteral("New sports name"), QStringLiteral("News")});
        QCOMPARE(first.at(0), second.at(1));
        QCOMPARE(first.at(1), second.at(0));
        QVERIFY(first.at(0) != first.at(1));
    }

    static void duplicateIdsUseNames()
    {
        const auto first =
            ChannelIdentity::forPlaylist({QStringLiteral("a"), QStringLiteral("a")},
                                         {QStringLiteral("News"), QStringLiteral("Sports")});
        const auto second =
            ChannelIdentity::forPlaylist({QStringLiteral("a"), QStringLiteral("a")},
                                         {QStringLiteral("Sports"), QStringLiteral("News")});
        QCOMPARE(first.at(0), second.at(1));
        QCOMPARE(first.at(1), second.at(0));
    }

    static void ambiguousAndUnnamedAreRejected()
    {
        const auto ids = ChannelIdentity::forPlaylist(
            {QString(), QString(), QString(), QString()},
            {QStringLiteral("News"), QStringLiteral("News"), QString(), QStringLiteral("Sports")});
        QVERIFY(ids.at(0).isEmpty());
        QVERIFY(ids.at(1).isEmpty());
        QVERIFY(ids.at(2).isEmpty());
        QVERIFY(!ids.at(3).isEmpty());
        QVERIFY(ChannelIdentity::forPlaylist({QStringLiteral("a")}, {}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(ChannelIdentityTest)
#include "test_channelidentity.moc"
