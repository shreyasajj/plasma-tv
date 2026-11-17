/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef GUIDETIMEZONE_H
#define GUIDETIMEZONE_H

#include <QDate>
#include <QTimeZone>

// Shared timezone rules for guide labels and provider requests. Epoch timestamps stay absolute.
namespace GuideTimeZone
{
// An empty ID follows the system timezone; invalid saved IDs also fall back to Auto.
QString normalize(const QString &id);
QTimeZone resolve(const QString &id, const QByteArray &systemId = QTimeZone::systemTimeZoneId());
QString formatTime(qint64 ms, const QTimeZone &zone);
QString formatDay(qint64 ms, qint64 nowMs, const QTimeZone &zone);
QList<QDate> dates(qint64 from, qint64 to, qint64 now, int days, const QTimeZone &zone);
}

#endif // GUIDETIMEZONE_H
