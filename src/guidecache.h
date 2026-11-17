/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef GUIDECACHE_H
#define GUIDECACHE_H

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>

// Stores guide responses for TvhApi across restarts. The API runs file work on its worker thread.
class GuideCache
{
public:
    using Clock = std::function<qint64()>;
    explicit GuideCache(QString directory, Clock clock = {});
    [[nodiscard]] std::optional<QByteArray> read(const QByteArray &key) const;
    void write(const QByteArray &key, const QByteArray &body) const;
    void clear() const;

private:
    [[nodiscard]] QString path(const QByteArray &key) const;
    QString m_directory;
    Clock m_clock;
};

#endif // GUIDECACHE_H
