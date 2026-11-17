// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "guidetimezone.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>

QString GuideTimeZone::normalize(const QString &id)
{
    const QString trimmed = id.trimmed();

    return QTimeZone::isTimeZoneIdAvailable(trimmed.toUtf8()) ? trimmed : QString();
}

QTimeZone GuideTimeZone::resolve(const QString &id, const QByteArray &systemId)
{
    const QString selected = normalize(id);
    const QTimeZone zone(selected.isEmpty() ? systemId : selected.toUtf8());

    return zone.isValid() ? zone : QTimeZone(QTimeZone::UTC);
}

QString GuideTimeZone::formatTime(qint64 ms, const QTimeZone &zone)
{
    return QDateTime::fromMSecsSinceEpoch(ms, zone).toString(QStringLiteral("HH:mm"));
}

QString GuideTimeZone::formatDay(qint64 ms, qint64 nowMs, const QTimeZone &zone)
{
    const QDate date = QDateTime::fromMSecsSinceEpoch(ms, zone).date();
    const QDate today = QDateTime::fromMSecsSinceEpoch(nowMs, zone).date();

    if (date == today) {
        return QCoreApplication::translate("GuideTimeZone", "Today");
    }

    if (date == today.addDays(1)) {
        return QCoreApplication::translate("GuideTimeZone", "Tomorrow");
    }

    return QLocale().toString(date, QStringLiteral("dddd, d MMMM"));
}

QList<QDate>
GuideTimeZone::dates(qint64 from, qint64 to, qint64 now, int days, const QTimeZone &zone)
{
    if (to <= from || days < 1) {
        return {};
    }

    const QDate today = QDateTime::fromSecsSinceEpoch(now, zone).date();
    const QDate first = qMax(today, QDateTime::fromSecsSinceEpoch(from, zone).date());
    const QDate last =
        qMin(today.addDays(days), QDateTime::fromSecsSinceEpoch(to - 1, zone).date());
    QList<QDate> result;

    for (QDate day = first; day <= last; day = day.addDays(1)) {
        result.append(day);
    }

    return result;
}
