/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STREAMBUFFER_H
#define STREAMBUFFER_H

#include <QByteArray>
#include <QElapsedTimer>

#include <cstdint>
#include <deque>
#include <functional>

// Holds back a few seconds of a live stream, so the player keeps playing when
// the server stalls or the relay reconnects. No network code: StreamProxy
// appends what the server sends and takes what the player can read.
//
// The length is measured with the PCR timestamps of MPEG-TS, because servers
// often send the first seconds faster than real time. Other streams fall back
// to the byte rate of the last seconds.
class StreamBuffer
{
public:
    // Buffer modes, in seconds. Auto starts at 0 and grows after each stall.
    static constexpr int Auto = -1;
    static constexpr int Off = 0;
    // Nearest supported mode: Auto, Off, 2, 5, 10, 20 or 30.
    static int normalizeMode(int seconds);

    static constexpr int s_packetSize = 188;
    // The player waited this long for data: grow the target (Auto) and refill.
    static constexpr qint64 s_stallMs = 1000;

    // Gets each aligned MPEG-TS packet with its media time in milliseconds.
    using PacketSink = std::function<void(const char *data, qint64 size, qint64 mediaMs)>;

    // 'clock' returns milliseconds; tests pass their own.
    explicit StreamBuffer(int mode, std::function<qint64()> clock = {});

    // Called for every packet appended from now on; empty to stop.
    void setPacketSink(PacketSink sink)
    {
        m_sink = std::move(sink);
    }
    // Hands the queued packets, those the player didn't take yet, to 'sink'.
    void replayQueued(const PacketSink &sink) const;
    // Drops the oldest queued data down to 'maxBytes', on a packet boundary.
    void keepNewest(qint64 maxBytes);

    void append(const QByteArray &data);
    // Up to 'maxBytes' for the player. Empty while filling up or when nothing is queued.
    QByteArray take(qint64 maxBytes);
    // Everything queued, whatever the target; the stream is about to end.
    QByteArray drain();
    // No more data follows: queue what was held back to decide the format.
    void endOfData();
    // The relay should stop reading from the server until the player took some.
    [[nodiscard]] bool full() const;
    // The upstream connection broke: forget a partial packet and find the
    // packet boundary again in what the next connection sends.
    void cutForReconnect();

    [[nodiscard]] qint64 size() const
    {
        return m_queue.size() - m_head;
    }
    [[nodiscard]] qint64 bufferedMs() const;
    [[nodiscard]] qint64 targetMs() const
    {
        return m_targetMs;
    }
    [[nodiscard]] bool filling() const
    {
        return m_filling;
    }
    [[nodiscard]] bool isTransportStream() const
    {
        return m_format == Format::TransportStream;
    }
    [[nodiscard]] bool formatKnown() const
    {
        return m_format != Format::Unknown;
    }

private:
    enum class Format : std::uint8_t {
        Unknown,
        TransportStream,
        Other,
    };

    struct Mark {
        qint64 offset; // total bytes appended before this packet
        qint64 media; // continuous media time of its PCR, 90 kHz
    };

    void detectFormat();
    void appendPackets();
    void appendBytes(const QByteArray &data);
    void readPcr(const char *packet);
    void stalled();
    [[nodiscard]] qint64 rateBytesPerSecond() const;
    [[nodiscard]] qint64 mediaMsAt(qint64 offset) const;
    void dropMarksBefore(qint64 offset);

    std::function<qint64()> m_clock;
    QElapsedTimer m_timer;

    int m_mode;
    qint64 m_targetMs = 0;
    bool m_filling = false;

    Format m_format = Format::Unknown;
    QByteArray m_pending; // not yet split into packets
    bool m_resync = true;

    QByteArray m_queue;
    qint64 m_head = 0; // read position in m_queue
    qint64 m_appended = 0; // total bytes queued
    qint64 m_taken = 0; // total bytes handed to the player

    // PCR of one PID, as a clock that keeps running over wrap-arounds and gaps.
    int m_pcrPid = -1;
    qint64 m_lastPcr = -1;
    qint64 m_media = 0;
    std::deque<Mark> m_marks;
    PacketSink m_sink;

    // Byte rate for streams without PCR: (time, total appended) samples.
    std::deque<std::pair<qint64, qint64>> m_samples;

    qint64 m_emptySince = -1;
};

#endif // STREAMBUFFER_H
