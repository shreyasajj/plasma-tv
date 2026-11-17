// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "guidetimezone.h"

#include <QDateTime>
#include <QTest>

// Exercises guide date boundaries and labels without depending on the machine's clock or zone.
class GuideTimeZoneTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void automaticallyUsesTheSystemZone()
    {
        QCOMPARE(GuideTimeZone::resolve({}, "America/Chicago").id(), QByteArray("America/Chicago"));
        QCOMPARE(GuideTimeZone::resolve({}, "Asia/Kolkata").id(), QByteArray("Asia/Kolkata"));
        QCOMPARE(GuideTimeZone::resolve({}, "Invalid/Zone"), QTimeZone(QTimeZone::UTC));
    }

    void manualChoiceOverridesTheSystem()
    {
        QCOMPARE(GuideTimeZone::resolve(QStringLiteral("Europe/Berlin"), "America/Chicago").id(),
                 QByteArray("Europe/Berlin"));
        QCOMPARE(GuideTimeZone::normalize(QStringLiteral(" America/Chicago ")),
                 QStringLiteral("America/Chicago"));
        QVERIFY(GuideTimeZone::normalize(QStringLiteral("Invalid/Zone")).isEmpty());
        QCOMPARE(GuideTimeZone::resolve(QStringLiteral("Invalid/Zone"), "America/Chicago").id(),
                 QByteArray("America/Chicago"));
    }

    void formatsTheSameInstantInDifferentZones()
    {
        const qint64 instant =
            QDateTime::fromString(QStringLiteral("2026-11-17T02:30:00Z"), Qt::ISODate)
                .toMSecsSinceEpoch();
        QCOMPARE(GuideTimeZone::formatTime(instant, QTimeZone("America/Chicago")),
                 QStringLiteral("20:30"));
        QCOMPARE(GuideTimeZone::formatTime(instant, QTimeZone("Asia/Kolkata")),
                 QStringLiteral("08:00"));
        QCOMPARE(GuideTimeZone::formatDay(instant, instant, QTimeZone("America/Chicago")),
                 QStringLiteral("Today"));
    }

    void requestsTheLocalDateAcrossUtcMidnight()
    {
        const qint64 now =
            QDateTime::fromString(QStringLiteral("2026-11-17T02:30:00Z"), Qt::ISODate)
                .toSecsSinceEpoch();
        QCOMPARE(GuideTimeZone::dates(now, now + 3600, now, 3, QTimeZone("America/Chicago")),
                 QList<QDate>{QDate(2026, 11, 16)});
        QCOMPARE(GuideTimeZone::dates(now, now + 3600, now, 3, QTimeZone("Asia/Kolkata")),
                 QList<QDate>{QDate(2026, 11, 17)});
    }

    void followsDaylightSavingTransitions()
    {
        const QTimeZone chicago("America/Chicago");
        const qint64 before =
            QDateTime::fromString(QStringLiteral("2026-03-08T07:30:00Z"), Qt::ISODate)
                .toMSecsSinceEpoch();
        const qint64 after =
            QDateTime::fromString(QStringLiteral("2026-03-08T08:30:00Z"), Qt::ISODate)
                .toMSecsSinceEpoch();
        QCOMPARE(GuideTimeZone::formatTime(before, chicago), QStringLiteral("01:30"));
        QCOMPARE(GuideTimeZone::formatTime(after, chicago), QStringLiteral("03:30"));
        const qint64 start = QDateTime(QDate(2026, 3, 8), QTime(0, 0), chicago).toSecsSinceEpoch();
        const qint64 end = QDateTime(QDate(2026, 3, 9), QTime(0, 0), chicago).toSecsSinceEpoch();
        QCOMPARE(end - start, 23 * 3600);
        QCOMPARE(GuideTimeZone::dates(start, end, start, 3, chicago),
                 QList<QDate>{QDate(2026, 3, 8)});
    }

    void includesBothDatesWhenTheWindowCrossesLocalMidnight()
    {
        const qint64 now =
            QDateTime::fromString(QStringLiteral("2026-11-17T05:30:00Z"), Qt::ISODate)
                .toSecsSinceEpoch();
        const QList<QDate> expected{QDate(2026, 11, 16), QDate(2026, 11, 17)};
        QCOMPARE(GuideTimeZone::dates(now, now + 3600, now, 3, QTimeZone("America/Chicago")),
                 expected);
        QVERIFY(GuideTimeZone::dates(now, now, now, 3, QTimeZone("America/Chicago")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(GuideTimeZoneTest)

#include "test_guidetimezone.moc"
