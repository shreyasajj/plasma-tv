// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "streambuffer.h"

#include <array>
#include <cstdlib>

namespace
{
constexpr char s_sync = 0x47;
// PCR runs at 90 kHz and wraps after 33 bits.
constexpr qint64 s_pcrPerMs = 90;
constexpr qint64 s_pcrMask = (qint64(1) << 33) - 1;
// A larger jump between two PCRs is a gap in the stream, not playing time.
constexpr qint64 s_maxPcrStep = 10 * 1000 * s_pcrPerMs;
// Junk the start of a stream may have before the first packet.
constexpr qsizetype s_maxSyncSearch = 8 * 1024;
// Bytes are queued up to this size whatever the target is.
constexpr qint64 s_minFull = 4 * 1024 * 1024;
// ...and never more than this.
constexpr qint64 s_maxFull = 64 * 1024 * 1024;
// Byte rate window for streams without PCR.
constexpr qint64 s_rateWindowMs = 10000;
constexpr qint64 s_minRateSpanMs = 500;
// Auto grows to the next of these after each stall.
constexpr std::array<qint64, 4> s_autoSteps = {0, 2000, 5000, 10000};
}

int StreamBuffer::normalizeMode(int seconds)
{
    if (seconds < 0) {
        return Auto;
    }

    int best = 0;

    for (const int mode : {0, 2, 5, 10}) {
        if (std::abs(seconds - mode) < std::abs(seconds - best)) {
            best = mode;
        }
    }

    return best;
}

StreamBuffer::StreamBuffer(int mode, std::function<qint64()> clock)
    : m_clock(std::move(clock))
    , m_mode(normalizeMode(mode))
{
    if (!m_clock) {
        m_timer.start();
        m_clock = [timer = m_timer]() {
            return timer.elapsed();
        };
    }

    m_targetMs = m_mode > 0 ? m_mode * 1000 : 0;
    m_filling = m_targetMs > 0;
}

void StreamBuffer::append(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    const qint64 now = m_clock();

    // The player ran dry and waited: hold back more from now on.
    if (m_emptySince >= 0 && now - m_emptySince >= s_stallMs) {
        stalled();
    }

    m_emptySince = -1;

    if (m_format == Format::Other) {
        appendBytes(data);
    } else {
        m_pending.append(data);

        if (m_format == Format::Unknown) {
            detectFormat();
        }

        if (m_format == Format::TransportStream) {
            appendPackets();
        } else if (m_format == Format::Other) {
            appendBytes(m_pending);
            m_pending.clear();
        }
    }

    m_samples.emplace_back(now, m_appended);

    while (m_samples.size() > 2 && now - m_samples[1].first >= s_rateWindowMs) {
        m_samples.pop_front();
    }
}

void StreamBuffer::detectFormat()
{
    // Three packet starts in a row make it MPEG-TS.
    const qsizetype last = m_pending.size() - 2 * s_packetSize;

    for (qsizetype i = 0; i < last && i < s_maxSyncSearch; ++i) {
        if (m_pending.at(i) == s_sync && m_pending.at(i + s_packetSize) == s_sync
            && m_pending.at(i + 2 * s_packetSize) == s_sync) {
            m_format = Format::TransportStream;

            return;
        }
    }

    if (last >= s_maxSyncSearch) {
        m_format = Format::Other;
    }
}

void StreamBuffer::appendPackets()
{
    qsizetype pos = 0;

    while (true) {
        if (m_resync) {
            // A packet start is only trusted when the next packet starts where expected.
            qsizetype found = -1;

            for (qsizetype i = pos; i + s_packetSize < m_pending.size(); ++i) {
                if (m_pending.at(i) == s_sync && m_pending.at(i + s_packetSize) == s_sync) {
                    found = i;

                    break;
                }
            }

            if (found < 0) {
                // Keep a packet's worth of bytes to search again with more data.
                pos = qMax(pos, m_pending.size() - s_packetSize);

                break;
            }

            pos = found;
            m_resync = false;
        }

        if (m_pending.size() - pos < s_packetSize) {
            break;
        }

        if (m_pending.at(pos) != s_sync) {
            m_resync = true;

            continue;
        }

        readPcr(m_pending.constData() + pos);
        m_queue.append(m_pending.constData() + pos, s_packetSize);
        m_appended += s_packetSize;
        pos += s_packetSize;
    }

    m_pending.remove(0, pos);
}

void StreamBuffer::appendBytes(const QByteArray &data)
{
    m_queue.append(data);
    m_appended += data.size();
}

void StreamBuffer::readPcr(const char *packet)
{
    const auto *p = reinterpret_cast<const unsigned char *>(packet);
    const bool hasAdaptation = p[3] & 0x20;

    // Adaptation field long enough for a PCR, with the PCR flag set.
    if (!hasAdaptation || p[4] < 7 || !(p[5] & 0x10)) {
        return;
    }

    // Programmes may carry PCR on several PIDs; one is enough.
    const int pid = ((p[1] & 0x1f) << 8) | p[2];

    if (m_pcrPid < 0) {
        m_pcrPid = pid;
    } else if (pid != m_pcrPid) {
        return;
    }

    const qint64 pcr = (qint64(p[6]) << 25) | (qint64(p[7]) << 17) | (qint64(p[8]) << 9)
        | (qint64(p[9]) << 1) | (qint64(p[10]) >> 7);

    if (m_lastPcr >= 0) {
        const qint64 step = (pcr - m_lastPcr) & s_pcrMask;

        if (step <= s_maxPcrStep) {
            m_media += step;
        }
    }

    m_lastPcr = pcr;
    m_marks.push_back({m_appended, m_media});
}

QByteArray StreamBuffer::take(qint64 maxBytes)
{
    if (m_filling) {
        if (bufferedMs() < m_targetMs && size() < s_maxFull) {
            return {};
        }

        m_filling = false;
    }

    if (size() == 0) {
        if (m_emptySince < 0) {
            m_emptySince = m_clock();
        }

        return {};
    }

    const qint64 count = qMin(maxBytes, size());
    QByteArray data = m_queue.mid(m_head, count);
    m_head += count;
    m_taken += count;

    // Drop what was sent, but not on every call.
    if (m_head > s_minFull && m_head * 2 > m_queue.size()) {
        m_queue.remove(0, m_head);
        m_head = 0;
    }

    while (m_marks.size() > 1 && m_marks[1].offset <= m_taken) {
        m_marks.pop_front();
    }

    if (size() == 0) {
        m_emptySince = m_clock();
    }

    return data;
}

QByteArray StreamBuffer::drain()
{
    m_filling = false;

    return take(size());
}

void StreamBuffer::endOfData()
{
    if (m_format == Format::Unknown) {
        m_format = Format::Other;
        appendBytes(m_pending);
        m_pending.clear();
    }
}

bool StreamBuffer::full() const
{
    if (size() >= s_maxFull) {
        return true;
    }

    if (size() < s_minFull || m_filling) {
        return false;
    }

    return bufferedMs() >= m_targetMs * 3 / 2;
}

void StreamBuffer::cutForReconnect()
{
    m_pending.clear();
    m_resync = true;
    // The next connection's PCR continues the media clock instead of counting the gap.
    m_lastPcr = -1;
}

qint64 StreamBuffer::bufferedMs() const
{
    if (size() == 0) {
        return 0;
    }

    if (m_pcrPid >= 0 && !m_marks.empty()) {
        return (m_marks.back().media - m_marks.front().media) / s_pcrPerMs;
    }

    const qint64 rate = rateBytesPerSecond();

    return rate > 0 ? size() * 1000 / rate : 0;
}

qint64 StreamBuffer::rateBytesPerSecond() const
{
    if (m_samples.size() < 2) {
        return 0;
    }

    const auto &[firstTime, firstBytes] = m_samples.front();
    const auto &[lastTime, lastBytes] = m_samples.back();
    const qint64 span = lastTime - firstTime;

    return span >= s_minRateSpanMs ? (lastBytes - firstBytes) * 1000 / span : 0;
}

void StreamBuffer::stalled()
{
    if (m_mode == Auto) {
        for (const qint64 step : s_autoSteps) {
            if (step > m_targetMs) {
                m_targetMs = step;

                break;
            }
        }
    }

    m_filling = m_targetMs > 0;
}
