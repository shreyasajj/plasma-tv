// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "timeshiftbuffer.h"

#include <QDebug>
#include <QDir>
#include <QStorageInfo>

#include <algorithm>
#include <cstdlib>
#include <initializer_list>
#include <iterator>
#include <utility>

namespace
{
constexpr qint64 s_markMs = 1000;

int nearest(int value, std::initializer_list<int> choices)
{
    int best = *choices.begin();

    for (const int choice : choices) {
        if (std::abs(value - choice) < std::abs(value - best)) {
            best = choice;
        }
    }

    return best;
}
}

int TimeshiftBuffer::normalizeRewindMinutes(int minutes)
{
    return nearest(minutes, {0, 15, 30, 60, 120});
}

int TimeshiftBuffer::normalizeMinFreeGb(int gigabytes)
{
    return nearest(gigabytes, {1, 2, 5, 10});
}

TimeshiftBuffer::TimeshiftBuffer(QString directory, Options options)
    : m_directory(std::move(directory))
    , m_options(std::move(options))
{
    if (!m_options.freeBytes) {
        m_options.freeBytes = [](const QString &path) {
            return QStorageInfo(path).bytesAvailable();
        };
    }

    QDir(m_directory).removeRecursively();

    if (!QDir().mkpath(m_directory)) {
        qWarning() << "Cannot create the rewind folder" << m_directory;
        m_stopped = true;
    }
}

TimeshiftBuffer::~TimeshiftBuffer()
{
    m_file.close();
    QDir(m_directory).removeRecursively();
}

void TimeshiftBuffer::write(const char *data, qint64 size, qint64 mediaMs)
{
    if (m_stopped || size <= 0) {
        return;
    }

    if (m_chunks.empty() || mediaMs - m_chunks.back().firstMs >= m_options.chunkMs) {
        if (!startChunk(mediaMs)) {
            return;
        }
    }

    if (m_file.write(data, size) != size) {
        qWarning() << "Cannot write the rewind file" << m_file.fileName() << m_file.errorString();
        m_stopped = true;

        return;
    }

    if (m_marks.empty() || mediaMs - m_marks.back().mediaMs >= s_markMs) {
        m_marks.push_back({.mediaMs = mediaMs, .position = m_written});
    }

    m_chunks.back().size += size;
    m_written += size;
    m_newestMs = std::max(m_newestMs, mediaMs);
    dropOld();
}

bool TimeshiftBuffer::startChunk(qint64 mediaMs)
{
    // The next chunk will be about as large as the last one.
    const qint64 expected = m_chunks.empty() ? 0 : m_chunks.back().size;

    if (m_options.freeBytes(m_directory) < m_options.minFreeBytes + expected) {
        qWarning() << "Rewinding stopped: the disk is nearly full";
        m_stopped = true;
        m_file.close();

        return false;
    }

    const int number = m_chunks.empty() ? 0 : m_chunks.back().number + 1;
    m_file.close();
    m_file.setFileName(chunkPath(number));

    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Cannot create the rewind file" << m_file.fileName() << m_file.errorString();
        m_stopped = true;

        return false;
    }

    m_chunks.push_back({.number = number, .start = m_written, .size = 0, .firstMs = mediaMs});

    return true;
}

void TimeshiftBuffer::dropOld()
{
    if (m_options.maxMs <= 0) {
        return;
    }

    // Delete a chunk once the rest still covers the whole length.
    while (m_chunks.size() > 1 && m_newestMs - m_chunks[1].firstMs >= m_options.maxMs) {
        QFile::remove(chunkPath(m_chunks.front().number));
        m_chunks.pop_front();
    }

    const qint64 oldest = oldestPosition();

    while (!m_marks.empty() && m_marks.front().position < oldest) {
        m_marks.pop_front();
    }
}

QByteArray TimeshiftBuffer::read(qint64 position, qint64 maxBytes)
{
    const auto chunk = std::ranges::find_if(m_chunks, [position](const Chunk &c) {
        return position >= c.start && position < c.start + c.size;
    });

    if (chunk == m_chunks.cend() || maxBytes <= 0) {
        return {};
    }

    if (chunk->number == m_chunks.back().number) {
        m_file.flush();
    }

    QFile file(chunkPath(chunk->number));

    if (!file.open(QIODevice::ReadOnly) || !file.seek(position - chunk->start)) {
        return {};
    }

    return file.read(std::min(maxBytes, chunk->start + chunk->size - position));
}

qint64 TimeshiftBuffer::positionAt(qint64 mediaMs) const
{
    if (m_marks.empty()) {
        return oldestPosition();
    }

    // The last mark at or before 'mediaMs'.
    const auto after = std::ranges::upper_bound(m_marks, mediaMs, {}, &Mark::mediaMs);

    return after == m_marks.cbegin() ? m_marks.front().position : std::prev(after)->position;
}

qint64 TimeshiftBuffer::oldestPosition() const
{
    return m_chunks.empty() ? m_written : m_chunks.front().start;
}

qint64 TimeshiftBuffer::oldestMs() const
{
    return m_marks.empty() ? m_newestMs : m_marks.front().mediaMs;
}

qint64 TimeshiftBuffer::windowMs() const
{
    return m_newestMs - oldestMs();
}

QString TimeshiftBuffer::chunkPath(int number) const
{
    return m_directory + QStringLiteral("/%1.ts").arg(number);
}

void TimeshiftBuffer::removeAll(const QString &root)
{
    QDir(root).removeRecursively();
}
