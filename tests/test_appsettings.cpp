// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "appsettings.h"

#include <QDateTime>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

// Checks saved timezone choices using isolated settings without configuring any provider.
class AppSettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("flimmer-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("timezone-settings"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_directory.path());
        QSettings().setValue(QStringLiteral("guide/timeZone"), QStringLiteral("Asia/Kolkata"));
    }

    void loadsTheSavedChoice()
    {
        QCOMPARE(AppSettings::instance()->guideTimeZone(), QStringLiteral("Asia/Kolkata"));
    }

    void savesTheManualChoiceAndNotifiesTheGuide()
    {
        AppSettings *settings = AppSettings::instance();
        QSignalSpy saved(settings, &AppSettings::saved);
        settings->save({{QStringLiteral("guideTimeZone"), QStringLiteral("Europe/Berlin")}});
        QCOMPARE(settings->guideTimeZone(), QStringLiteral("Europe/Berlin"));
        QCOMPARE(QSettings().value(QStringLiteral("guide/timeZone")).toString(),
                 QStringLiteral("Europe/Berlin"));
        QCOMPARE(saved.count(), 1);
    }

    void automaticAndInvalidChoicesFollowTheTv()
    {
        AppSettings *settings = AppSettings::instance();
        settings->save({{QStringLiteral("guideTimeZone"), QString()}});
        QVERIFY(settings->guideTimeZone().isEmpty());
        QVERIFY(QSettings().value(QStringLiteral("guide/timeZone")).toString().isEmpty());
        QVERIFY(!settings->detectedTimeZone().isEmpty());
        QVERIFY(settings->guideTimeZones()
                    .first()
                    .toMap()
                    .value(QStringLiteral("id"))
                    .toString()
                    .isEmpty());
        settings->save({{QStringLiteral("guideTimeZone"), QStringLiteral("Invalid/Zone")}});
        QVERIFY(settings->guideTimeZone().isEmpty());
    }

    void updatesTheDisplayedTimeAfterSaving()
    {
        AppSettings *settings = AppSettings::instance();
        const qint64 instant =
            QDateTime::fromString(QStringLiteral("2026-11-17T02:30:00Z"), Qt::ISODate)
                .toMSecsSinceEpoch();
        settings->save({{QStringLiteral("guideTimeZone"), QStringLiteral("America/Chicago")}});
        QCOMPARE(settings->formatGuideTime(instant), QStringLiteral("20:30"));
        settings->save({{QStringLiteral("guideTimeZone"), QStringLiteral("Asia/Kolkata")}});
        QCOMPARE(settings->formatGuideTime(instant), QStringLiteral("08:00"));
    }

private:
    QTemporaryDir m_directory;
};

QTEST_MAIN(AppSettingsTest)

#include "test_appsettings.moc"
