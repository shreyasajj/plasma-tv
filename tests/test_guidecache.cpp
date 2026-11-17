// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "guidecache.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// Verifies persistent guide freshness with an injected clock and isolated files.
class GuideCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void expiresAfterOneDayAndSurvivesRestart()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qint64 now = 100000;
        const auto clock = [&now]() {
            return now;
        };
        GuideCache cache(directory.path(), clock);
        QVERIFY(!cache.read("source/channel/day"));
        cache.write("source/channel/day", "guide");
        GuideCache restarted(directory.path(), clock);
        QCOMPARE(restarted.read("source/channel/day").value(), QByteArray("guide"));
        now += 86399;
        QVERIFY(restarted.read("source/channel/day"));
        ++now;
        QVERIFY(!restarted.read("source/channel/day"));
    }

    void cachesEmptyDaysAndKeepsSourcesAndDatesSeparate()
    {
        QTemporaryDir directory;
        GuideCache cache(directory.path(), []() {
            return 100000;
        });
        cache.write("first/day1", {});
        cache.write("second/day1", "second guide");
        QVERIFY(cache.read("first/day1").has_value());
        QVERIFY(cache.read("first/day1")->isEmpty());
        QCOMPARE(cache.read("second/day1").value(), QByteArray("second guide"));
        QVERIFY(!cache.read("first/day2"));
        cache.clear();
        QVERIFY(!cache.read("first/day1"));
        QVERIFY(!cache.read("second/day1"));
    }

    void rejectsClockRollbackAndCorruptFiles()
    {
        QTemporaryDir directory;
        qint64 now = 100000;
        GuideCache cache(directory.path(), [&now]() {
            return now;
        });
        cache.write("key", "guide");
        --now;
        QVERIFY(!cache.read("key"));
        const QStringList files = QDir(directory.path()).entryList({QStringLiteral("*.guide")});
        QCOMPARE(files.size(), 1);
        QFile file(QDir(directory.path()).filePath(files.first()));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("broken");
        file.close();
        QVERIFY(!cache.read("key"));
    }
};

QTEST_GUILESS_MAIN(GuideCacheTest)

#include "test_guidecache.moc"
