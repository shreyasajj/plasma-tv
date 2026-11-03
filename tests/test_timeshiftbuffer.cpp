// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "timeshiftbuffer.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

namespace
{
constexpr int s_packetSize = 188;

// A packet whose bytes tell the millisecond it belongs to.
QByteArray packet(qint64 ms)
{
    QByteArray p(s_packetSize, char(ms % 251));
    p[0] = 0x47;

    return p;
}

// One packet every 'stepMs' from 'fromMs' to 'toMs'; returns what was written.
QByteArray feed(TimeshiftBuffer &buffer, qint64 fromMs, qint64 toMs, qint64 stepMs = 100)
{
    QByteArray written;

    for (qint64 ms = fromMs; ms <= toMs; ms += stepMs) {
        const QByteArray p = packet(ms);
        buffer.write(p.constData(), p.size(), ms);
        written += p;
    }

    return written;
}

QByteArray readAll(TimeshiftBuffer &buffer, qint64 from)
{
    QByteArray data;
    qint64 position = from;

    while (true) {
        const QByteArray part = buffer.read(position, 1000);

        if (part.isEmpty()) {
            return data;
        }

        data += part;
        position += part.size();
    }
}

int chunkFiles(const QString &directory)
{
    return int(QDir(directory).entryList({QStringLiteral("*.ts")}, QDir::Files).size());
}

TimeshiftBuffer::Options options(qint64 maxMs, qint64 chunkMs = 1000)
{
    TimeshiftBuffer::Options o;
    o.maxMs = maxMs;
    o.chunkMs = chunkMs;
    o.freeBytes = [](const QString &) {
        return qint64(1) << 40;
    };

    return o;
}
}

class TimeshiftBufferTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizesSettings()
    {
        QCOMPARE(TimeshiftBuffer::normalizeRewindMinutes(-3), 0);
        QCOMPARE(TimeshiftBuffer::normalizeRewindMinutes(20), 15);
        QCOMPARE(TimeshiftBuffer::normalizeRewindMinutes(25), 30);
        QCOMPARE(TimeshiftBuffer::normalizeRewindMinutes(500), 120);
        QCOMPARE(TimeshiftBuffer::normalizeMinFreeGb(0), 1);
        QCOMPARE(TimeshiftBuffer::normalizeMinFreeGb(4), 5);
        QCOMPARE(TimeshiftBuffer::normalizeMinFreeGb(50), 10);
    }

    void readsBackWhatWasWritten()
    {
        QTemporaryDir root;
        TimeshiftBuffer buffer(root.filePath(QStringLiteral("a")), options(0));
        const QByteArray written = feed(buffer, 0, 3500);

        QCOMPARE(buffer.endPosition(), written.size());
        QCOMPARE(buffer.oldestPosition(), 0);
        QCOMPARE(buffer.newestMs(), 3500);
        QCOMPARE(buffer.windowMs(), 3500);
        // Across the chunk files, one per second.
        QCOMPARE(chunkFiles(root.filePath(QStringLiteral("a"))), 4);
        QCOMPARE(readAll(buffer, 0), written);
        QVERIFY(buffer.read(buffer.endPosition(), 1000).isEmpty());
    }

    void readerFollowsTheGrowingFile()
    {
        QTemporaryDir root;
        TimeshiftBuffer buffer(root.filePath(QStringLiteral("a")), options(0));
        QByteArray written = feed(buffer, 0, 900);
        QByteArray read = readAll(buffer, 0);
        QCOMPARE(read, written);

        written += feed(buffer, 1000, 2500);
        read += readAll(buffer, read.size());
        QCOMPARE(read, written);
    }

    void findsThePositionForATime()
    {
        QTemporaryDir root;
        TimeshiftBuffer buffer(root.filePath(QStringLiteral("a")), options(0));
        feed(buffer, 0, 5000);

        // One mark per second: 2500 ms starts at the packet of 2000 ms.
        QCOMPARE(buffer.positionAt(2500), 20 * s_packetSize);
        QCOMPARE(buffer.positionAt(2000), 20 * s_packetSize);
        QCOMPARE(buffer.positionAt(-100), 0);
        QCOMPARE(buffer.positionAt(99999), 50 * s_packetSize);
        QCOMPARE(buffer.read(buffer.positionAt(3000), s_packetSize), packet(3000));
    }

    void deletesWhatIsOlderThanTheLength()
    {
        QTemporaryDir root;
        const QString directory = root.filePath(QStringLiteral("a"));
        TimeshiftBuffer buffer(directory, options(2000));
        const QByteArray written = feed(buffer, 0, 5900);

        // At least the length is kept, at most a chunk more.
        QVERIFY(buffer.windowMs() >= 2000);
        QVERIFY(buffer.windowMs() <= 3000);
        QVERIFY(buffer.oldestPosition() > 0);
        QCOMPARE(chunkFiles(directory), 3);
        QVERIFY(buffer.read(0, 1000).isEmpty());
        QCOMPARE(readAll(buffer, buffer.oldestPosition()), written.mid(buffer.oldestPosition()));
        QCOMPARE(buffer.positionAt(0), buffer.oldestPosition());
    }

    void stopsWhenTheDiskIsNearlyFull()
    {
        QTemporaryDir root;
        qint64 free = qint64(1) << 30;
        TimeshiftBuffer::Options o = options(0);
        o.minFreeBytes = 1000;
        o.freeBytes = [&free](const QString &) {
            return free;
        };
        TimeshiftBuffer buffer(root.filePath(QStringLiteral("a")), o);
        const QByteArray written = feed(buffer, 0, 1900);
        QVERIFY(!buffer.stopped());

        // The next chunk needs the limit plus about one chunk.
        free = 1000 + written.size() / 4;
        feed(buffer, 2000, 3000);
        QVERIFY(buffer.stopped());
        QCOMPARE(buffer.endPosition(), written.size());
        QCOMPARE(readAll(buffer, 0), written);
    }

    void cleansUp()
    {
        QTemporaryDir root;
        const QString directory = root.filePath(QStringLiteral("timeshift/a"));
        {
            TimeshiftBuffer buffer(directory, options(0));
            feed(buffer, 0, 1000);
            QVERIFY(QFileInfo::exists(directory));
        }
        QVERIFY(!QFileInfo::exists(directory));

        // Left over by a crash.
        QVERIFY(QDir().mkpath(directory));
        TimeshiftBuffer::removeAll(root.filePath(QStringLiteral("timeshift")));
        QVERIFY(!QFileInfo::exists(root.filePath(QStringLiteral("timeshift"))));
    }
};

QTEST_GUILESS_MAIN(TimeshiftBufferTest)

#include "test_timeshiftbuffer.moc"
