// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "epgdatabase.h"

#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>
#include <optional>

namespace
{
Channel channel(const QString &uuid, int number, const QString &stream = {})
{
    Channel result;
    result.uuid = uuid;
    result.number = number;
    result.name = uuid.toUpper();
    result.stream = stream;

    return result;
}

EpgEvent makeEvent(qint64 id, const QString &channel, qint64 start, qint64 stop)
{
    EpgEvent result;
    result.id = id;
    result.channel = channel;
    result.start = start;
    result.stop = stop;
    result.title = QStringLiteral("Event %1").arg(id);
    result.genre = 0x10;

    return result;
}

class EpgDatabaseTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dataDir;

    // Waits for the events loaded on the database thread.
    QVector<EpgEvent> loadEvents(const EpgDatabase &database, qint64 notEndedBefore)
    {
        std::optional<QVector<EpgEvent>> loaded;
        database.loadEvents(notEndedBefore, this, [&loaded](QVector<EpgEvent> events) {
            loaded = std::move(events);
        });

        if (!QTest::qWaitFor([&loaded]() {
                return loaded.has_value();
            })) {
            return {};
        }

        return *loaded;
    }

private Q_SLOTS:
    void initTestCase()
    {
        // Keeps the test away from the user's cache; the folder is deleted afterwards.
        QVERIFY(m_dataDir.isValid());
        qputenv("XDG_DATA_HOME", QFile::encodeName(m_dataDir.path()));
    }

    static void init()
    {
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    }

    static void channels()
    {
        EpgDatabase database;
        QVERIFY(database.open());

        database.saveChannels(QStringLiteral("s1"),
                              {
                                  channel(QStringLiteral("s1:b"), 2),
                                  channel(QStringLiteral("s1:a"), 1, QStringLiteral("http://a/1")),
                              });

        const QVector<Channel> loaded = database.loadChannels();

        // The order is kept as saved.
        QCOMPARE(loaded.size(), 2);
        QCOMPARE(loaded.at(0).uuid, QStringLiteral("s1:b"));
        QCOMPARE(loaded.at(1).name, QStringLiteral("S1:A"));
        QCOMPARE(loaded.at(1).stream, QStringLiteral("http://a/1"));

        database.saveChannels(QStringLiteral("s1"), {channel(QStringLiteral("s1:c"), 3)});
        QCOMPARE(database.loadChannels().size(), 1);
    }

    void events()
    {
        EpgDatabase database;
        QVERIFY(database.open());

        database.saveEvents(QStringLiteral("s1"),
                            {
                                makeEvent(1, QStringLiteral("s1:a"), 100, 200),
                                makeEvent(2, QStringLiteral("s1:a"), 200, 300),
                            },
                            12345);

        const QVector<EpgEvent> loaded = loadEvents(database, 0);

        QCOMPARE(loaded.size(), 2);
        QCOMPARE(database.lastUpdate(), 12345);

        const auto first = std::ranges::find_if(loaded, [](const EpgEvent &e) {
            return e.id == 1;
        });

        QVERIFY(first != loaded.cend());
        QCOMPARE(first->title, QStringLiteral("Event 1"));
        QCOMPARE(first->stop, 200);
        QCOMPARE(first->genre, 0x10);

        // Events that are over aren't loaded.
        QCOMPARE(loadEvents(database, 250).size(), 1);
        QCOMPARE(loadEvents(database, 300).size(), 0);
    }

    void updateRecording()
    {
        EpgDatabase database;
        QVERIFY(database.open());

        database.saveEvents(QStringLiteral("s1"),
                            {makeEvent(1, QStringLiteral("s1:a"), 100, 200)},
                            1);
        database.updateRecording(1, QStringLiteral("dvr-1"), QStringLiteral("scheduled"));
        const QVector<EpgEvent> loaded = loadEvents(database, 0);

        QCOMPARE(loaded.size(), 1);
        QCOMPARE(loaded.first().dvrUuid, QStringLiteral("dvr-1"));
        QCOMPARE(loaded.first().dvrState, QStringLiteral("scheduled"));
    }

    void identityAndClear()
    {
        EpgDatabase database;
        QVERIFY(database.open());
        QVERIFY(database.sourceIdentity(QStringLiteral("s1")).isEmpty());

        database.setSourceIdentity(QStringLiteral("s1"), QStringLiteral("http://tvh:9981/"));
        database.saveChannels(QStringLiteral("s1"), {channel(QStringLiteral("s1:a"), 1)});
        database.saveEvents(QStringLiteral("s1"),
                            {makeEvent(1, QStringLiteral("s1:a"), 100, 200)},
                            1);
        QCOMPARE(database.sourceIdentity(QStringLiteral("s1")), QStringLiteral("http://tvh:9981/"));

        database.clear();

        QVERIFY(database.sourceIdentity(QStringLiteral("s1")).isEmpty());
        QVERIFY(database.loadChannels().isEmpty());
        QVERIFY(loadEvents(database, 0).isEmpty());
        QCOMPARE(database.lastUpdate(), 0);
    }

    void sourcesAreKeptApart()
    {
        EpgDatabase database;
        QVERIFY(database.open());

        database.setSourceIdentity(QStringLiteral("s1"), QStringLiteral("one"));
        database.setSourceIdentity(QStringLiteral("s10"), QStringLiteral("ten"));
        database.saveChannels(QStringLiteral("s1"), {channel(QStringLiteral("s1:a"), 1)});
        database.saveChannels(QStringLiteral("s10"), {channel(QStringLiteral("s10:a"), 1)});
        database.saveEvents(QStringLiteral("s1"),
                            {makeEvent(1, QStringLiteral("s1:a"), 100, 200)},
                            1);
        database.saveEvents(QStringLiteral("s10"),
                            {makeEvent(2, QStringLiteral("s10:a"), 100, 200)},
                            1);

        // Saving one source doesn't touch the other, even with a similar ID.
        database.saveChannels(QStringLiteral("s1"), {channel(QStringLiteral("s1:b"), 2)});
        QCOMPARE(database.loadChannels().size(), 2);

        database.clearSource(QStringLiteral("s1"));
        const QVector<Channel> channels = database.loadChannels();
        QCOMPARE(channels.size(), 1);
        QCOMPARE(channels.first().uuid, QStringLiteral("s10:a"));
        QCOMPARE(loadEvents(database, 0).size(), 1);
        QVERIFY(database.sourceIdentity(QStringLiteral("s1")).isEmpty());
        QCOMPARE(database.sourceIdentity(QStringLiteral("s10")), QStringLiteral("ten"));

        // Identities are read back when opened again.
        EpgDatabase reopened;
        QVERIFY(reopened.open());
        QCOMPARE(reopened.sources(), QStringList{QStringLiteral("s10")});
    }

    void pendingWritesSurviveClosing()
    {
        {
            auto database = std::make_unique<EpgDatabase>();
            QVERIFY(database->open());
            database->setSourceIdentity(QStringLiteral("s1"), QStringLiteral("http://tvh:9981/"));
            database->saveEvents(QStringLiteral("s1"),
                                 {makeEvent(1, QStringLiteral("s1:a"), 100, 200)},
                                 42);
            // Destroyed right away, with the writes still queued.
        }

        EpgDatabase database;
        QVERIFY(database.open());

        QCOMPARE(database.sourceIdentity(QStringLiteral("s1")), QStringLiteral("http://tvh:9981/"));
        QCOMPARE(database.lastUpdate(), 42);
        QCOMPARE(loadEvents(database, 0).size(), 1);
    }
};

}

QTEST_GUILESS_MAIN(EpgDatabaseTest)

#include "test_epgdatabase.moc"
