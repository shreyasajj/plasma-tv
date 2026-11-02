// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streambuffer.h"

#include <QTest>

namespace
{
constexpr int s_pid = 0x100;

// One MPEG-TS packet, with a PCR (90 kHz) when 'pcr' isn't negative.
QByteArray packet(qint64 pcr = -1, int pid = s_pid)
{
    QByteArray p(StreamBuffer::s_packetSize, char(0xff));
    p[0] = 0x47;
    p[1] = char((pid >> 8) & 0x1f);
    p[2] = char(pid & 0xff);

    if (pcr < 0) {
        p[3] = 0x10; // payload only

        return p;
    }

    p[3] = 0x30; // adaptation field and payload
    p[4] = 7;
    p[5] = 0x10; // PCR flag
    p[6] = char(pcr >> 25);
    p[7] = char(pcr >> 17);
    p[8] = char(pcr >> 9);
    p[9] = char(pcr >> 1);
    p[10] = char(((pcr & 1) << 7) | 0x7e);
    p[11] = 0;

    return p;
}

// A PCR every 100 ms from 'fromMs' to 'toMs', with a few payload packets in between.
QByteArray stream(qint64 fromMs, qint64 toMs, qint64 pcrOffset = 0)
{
    QByteArray data;

    for (qint64 ms = fromMs; ms <= toMs; ms += 100) {
        data += packet((pcrOffset + ms * 90) & ((qint64(1) << 33) - 1));

        for (int i = 0; i < 4; ++i) {
            data += packet();
        }
    }

    return data;
}

bool aligned(const QByteArray &data)
{
    if (data.size() % StreamBuffer::s_packetSize != 0) {
        return false;
    }

    for (qsizetype i = 0; i < data.size(); i += StreamBuffer::s_packetSize) {
        if (data.at(i) != 0x47) {
            return false;
        }
    }

    return true;
}
}

class StreamBufferTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizesModes()
    {
        QCOMPARE(StreamBuffer::normalizeMode(-5), StreamBuffer::Auto);
        QCOMPARE(StreamBuffer::normalizeMode(-1), StreamBuffer::Auto);
        QCOMPARE(StreamBuffer::normalizeMode(0), StreamBuffer::Off);
        QCOMPARE(StreamBuffer::normalizeMode(2), 2);
        QCOMPARE(StreamBuffer::normalizeMode(3), 2);
        QCOMPARE(StreamBuffer::normalizeMode(4), 5);
        QCOMPARE(StreamBuffer::normalizeMode(8), 10);
        QCOMPARE(StreamBuffer::normalizeMode(30), 10);
    }

    void skipsJunkBeforeFirstPacket()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        const QByteArray ts = stream(0, 500);
        buffer.append(QByteArray(50, 'x') + ts);

        QVERIFY(buffer.isTransportStream());
        QCOMPARE(buffer.size(), ts.size());
        QCOMPARE(buffer.take(1 << 20), ts);
    }

    void waitsForCompletePackets()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        const QByteArray ts = stream(0, 1000);
        buffer.append(ts.left(1000));
        buffer.append(ts.mid(1000, 77));
        buffer.append(ts.mid(1077));

        QCOMPARE(buffer.size(), ts.size());
        QVERIFY(aligned(buffer.take(1 << 20)));
    }

    void measuresWithPcr()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        buffer.append(stream(0, 3000));

        QCOMPARE(buffer.bufferedMs(), 3000);

        // Taking half leaves about half.
        buffer.take(buffer.size() / 2);
        QVERIFY(buffer.bufferedMs() >= 1400 && buffer.bufferedMs() <= 1600);
    }

    void pcrWrapsAround()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        // Starts one second before the 33-bit PCR wraps.
        const qint64 offset = (qint64(1) << 33) - 90 * 1000;
        buffer.append(stream(0, 3000, offset));

        QCOMPARE(buffer.bufferedMs(), 3000);
    }

    void holdsUntilTarget()
    {
        StreamBuffer buffer(2);
        QVERIFY(buffer.filling());
        QCOMPARE(buffer.targetMs(), 2000);

        buffer.append(stream(0, 1000));
        QVERIFY(buffer.take(1 << 20).isEmpty());

        buffer.append(stream(1100, 2500));
        QVERIFY(!buffer.take(1 << 20).isEmpty());
        QVERIFY(!buffer.filling());
    }

    void autoGrowsAfterStalls()
    {
        qint64 now = 0;
        StreamBuffer buffer(StreamBuffer::Auto, [&now]() {
            return now;
        });

        // Starts instantly.
        QCOMPARE(buffer.targetMs(), 0);
        buffer.append(stream(0, 1000));
        QVERIFY(!buffer.take(1 << 24).isEmpty());

        qint64 ms = 1100;
        for (const qint64 expected : {2000, 5000, 10000, 10000}) {
            // The player emptied the buffer and waited longer than a stall.
            QVERIFY(buffer.take(1 << 24).isEmpty());
            now += StreamBuffer::s_stallMs + 500;
            buffer.append(stream(ms, ms + 100));
            ms += 200;

            QCOMPARE(buffer.targetMs(), expected);
            QVERIFY(buffer.filling());
            QVERIFY(buffer.take(1 << 24).isEmpty());

            buffer.append(stream(ms, ms + expected));
            ms += expected + 100;
            QVERIFY(!buffer.take(1 << 24).isEmpty());
            QCOMPARE(buffer.size(), 0);
        }
    }

    void shortGapIsNoStall()
    {
        qint64 now = 0;
        StreamBuffer buffer(StreamBuffer::Auto, [&now]() {
            return now;
        });

        buffer.append(stream(0, 500));
        buffer.take(1 << 24);
        now += StreamBuffer::s_stallMs / 2;
        buffer.append(stream(600, 1000));

        QCOMPARE(buffer.targetMs(), 0);
        QVERIFY(!buffer.filling());
    }

    void offNeverHolds()
    {
        qint64 now = 0;
        StreamBuffer buffer(StreamBuffer::Off, [&now]() {
            return now;
        });

        buffer.append(stream(0, 500));
        buffer.take(1 << 24);
        QVERIFY(buffer.take(1 << 24).isEmpty());
        now += 10 * StreamBuffer::s_stallMs;
        buffer.append(stream(600, 1000));

        QCOMPARE(buffer.targetMs(), 0);
        QVERIFY(!buffer.take(1 << 24).isEmpty());
    }

    void fixedModeRefillsAfterStall()
    {
        qint64 now = 0;
        StreamBuffer buffer(5, [&now]() {
            return now;
        });

        buffer.append(stream(0, 5000));
        QVERIFY(!buffer.take(1 << 24).isEmpty());
        QVERIFY(buffer.take(1 << 24).isEmpty());

        now += 2 * StreamBuffer::s_stallMs;
        buffer.append(stream(5100, 6000));

        QCOMPARE(buffer.targetMs(), 5000);
        QVERIFY(buffer.filling());
        QVERIFY(buffer.take(1 << 24).isEmpty());
    }

    void usesByteRateWithoutPcr()
    {
        qint64 now = 0;
        StreamBuffer buffer(StreamBuffer::Off, [&now]() {
            return now;
        });

        // 1 MB/s of data that isn't MPEG-TS.
        for (int i = 0; i <= 20; ++i) {
            buffer.append(QByteArray(100 * 1000, char(i)));
            now += 100;
        }

        QVERIFY(!buffer.isTransportStream());
        QCOMPARE(buffer.size(), 21 * 100 * 1000);
        QVERIFY(buffer.bufferedMs() >= 2000 && buffer.bufferedMs() <= 2200);
    }

    void joinsStreamsAfterReconnect()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        buffer.append(stream(0, 2000));
        // The connection broke in the middle of a packet.
        buffer.append(packet(-1).left(100));
        buffer.cutForReconnect();

        // The new connection starts mid-packet, with the server's clock 48 s later.
        const QByteArray next = stream(50000, 51000);
        buffer.append(next.mid(30));

        QVERIFY(aligned(buffer.take(1 << 24)));

        StreamBuffer counted(StreamBuffer::Off);
        counted.append(stream(0, 2000));
        counted.cutForReconnect();
        counted.append(next);
        // The gap between the connections isn't counted as playing time.
        QCOMPARE(counted.bufferedMs(), 3000);
    }

    void drainsWhileFilling()
    {
        StreamBuffer buffer(10);
        const QByteArray ts = stream(0, 1000);
        buffer.append(ts);

        QVERIFY(buffer.take(1 << 24).isEmpty());
        QCOMPARE(buffer.drain(), ts);
    }

    void shortStreamEndsWithoutFormat()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        buffer.append("#EXTM3U\n");
        QCOMPARE(buffer.size(), 0);

        buffer.endOfData();
        QCOMPARE(buffer.take(1024), QByteArray("#EXTM3U\n"));
    }

    void fullAtLimit()
    {
        StreamBuffer buffer(StreamBuffer::Off);
        QVERIFY(!buffer.full());

        while (buffer.size() < 5 * 1024 * 1024) {
            buffer.append(stream(0, 1000));
        }

        QVERIFY(buffer.full());
        buffer.take(2 * 1024 * 1024);
        QVERIFY(!buffer.full());
    }
};

QTEST_GUILESS_MAIN(StreamBufferTest)

#include "test_streambuffer.moc"
