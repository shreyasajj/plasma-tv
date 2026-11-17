// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "guidecache.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <utility>

namespace
{
// Guide responses stay fresh for a full day, including after restarting the app.
constexpr qint64 s_cacheSeconds = 24LL * 60 * 60;
// Identify our binary format before reading a timestamp or payload.
constexpr quint32 s_magic = 0x46475031;
// Remove unused channel/date entries after a month to bound disk use over time.
constexpr qint64 s_retentionSeconds = 30 * s_cacheSeconds;
}

GuideCache::GuideCache(QString directory, Clock clock)
    : m_directory(std::move(directory))
    , m_clock(clock ? std::move(clock) : Clock(QDateTime::currentSecsSinceEpoch))
{
}

QString GuideCache::path(const QByteArray &key) const
{
    return QDir(m_directory)
        .filePath(
            QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex())
            + QStringLiteral(".guide"));
}

std::optional<QByteArray> GuideCache::read(const QByteArray &key) const
{
    QFile file(path(key));

    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_5);
    quint32 magic = 0;
    qint64 fetchedAt = 0;
    stream >> magic >> fetchedAt;
    const qint64 now = m_clock();

    if (stream.status() != QDataStream::Ok || magic != s_magic || fetchedAt > now
        || now - fetchedAt >= s_cacheSeconds) {
        return std::nullopt;
    }

    QByteArray body;
    stream >> body;

    return stream.status() == QDataStream::Ok ? std::optional<QByteArray>(body) : std::nullopt;
}

void GuideCache::write(const QByteArray &key, const QByteArray &body) const
{
    if (!QDir().mkpath(m_directory)) {
        return;
    }

    QSaveFile file(path(key));

    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << s_magic << m_clock() << body;

    if (stream.status() != QDataStream::Ok || !file.commit()) {
        return;
    }

    const auto files = QDir(m_directory).entryInfoList({QStringLiteral("*.guide")}, QDir::Files);

    for (const QFileInfo &entry : files) {
        if (m_clock() - entry.lastModified().toSecsSinceEpoch() > s_retentionSeconds) {
            QFile::remove(entry.absoluteFilePath());
        }
    }
}

void GuideCache::clear() const
{
    const auto files = QDir(m_directory).entryInfoList({QStringLiteral("*.guide")}, QDir::Files);

    for (const QFileInfo &entry : files) {
        QFile::remove(entry.absoluteFilePath());
    }
}
