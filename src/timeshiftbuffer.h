/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TIMESHIFTBUFFER_H
#define TIMESHIFTBUFFER_H

#include <QByteArray>
#include <QFile>
#include <QString>

#include <deque>
#include <functional>

// Writes the MPEG-TS packets of one channel to chunk files on disk, so playback can
// start at an earlier moment. No network code: StreamProxy feeds it the packets of
// StreamBuffer with their media time and reads them back at a byte position.
//
// Positions count all bytes ever written, so they stay valid while old chunks are
// deleted; a position before the oldest chunk is no longer available.
class TimeshiftBuffer
{
public:
    struct Options {
        // Oldest data is deleted when it is older than this; 0 keeps everything.
        qint64 maxMs = 0;
        // Writing stops when the disk would have less than this free afterwards.
        qint64 minFreeBytes = 0;
        // Media time per chunk file.
        qint64 chunkMs = 60000;
        // Free bytes on the disk of 'directory'; tests pass their own.
        std::function<qint64(const QString &directory)> freeBytes;
    };

    // 'directory' is created and owned: it is deleted with the buffer.
    TimeshiftBuffer(QString directory, Options options);
    ~TimeshiftBuffer();

    TimeshiftBuffer(const TimeshiftBuffer &) = delete;
    TimeshiftBuffer &operator=(const TimeshiftBuffer &) = delete;

    // One packet (or several) that start at 'mediaMs'. Ignored once writing stopped.
    void write(const char *data, qint64 size, qint64 mediaMs);

    // Up to 'maxBytes' from 'position'. Empty when nothing newer is written yet.
    [[nodiscard]] QByteArray read(qint64 position, qint64 maxBytes);

    // Position of the packet nearest to 'mediaMs', at most one second earlier.
    [[nodiscard]] qint64 positionAt(qint64 mediaMs) const;

    [[nodiscard]] qint64 oldestPosition() const;
    [[nodiscard]] qint64 endPosition() const
    {
        return m_written;
    }
    [[nodiscard]] qint64 oldestMs() const;
    [[nodiscard]] qint64 newestMs() const
    {
        return m_newestMs;
    }
    // Media time that can be rewound.
    [[nodiscard]] qint64 windowMs() const;

    // The disk got too full (or a write failed): nothing more is written.
    [[nodiscard]] bool stopped() const
    {
        return m_stopped;
    }

    // Nearest supported settings: 0 (off), 15, 30, 60 or 120 minutes; 1, 2, 5 or 10 GB.
    static int normalizeRewindMinutes(int minutes);
    static int normalizeMinFreeGb(int gigabytes);

    // Deletes folders of buffers that weren't cleaned up, e.g. after a crash.
    static void removeAll(const QString &root);

private:
    struct Chunk {
        int number;
        qint64 start; // position of its first byte
        qint64 size;
        qint64 firstMs;
    };

    struct Mark {
        qint64 mediaMs;
        qint64 position;
    };

    bool startChunk(qint64 mediaMs);
    void dropOld();
    [[nodiscard]] QString chunkPath(int number) const;

    QString m_directory;
    Options m_options;
    std::deque<Chunk> m_chunks;
    std::deque<Mark> m_marks; // one per second of media time
    QFile m_file; // the chunk being written
    qint64 m_written = 0;
    qint64 m_newestMs = 0;
    bool m_stopped = false;
};

#endif // TIMESHIFTBUFFER_H
