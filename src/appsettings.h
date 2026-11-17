/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QObject>
#include <QVariantMap>
#include <qqmlregistration.h>

class QQmlEngine;
class QJSEngine;
class SourceSettings;

// Persistent application settings in the config file
// (~/.config/flimmer/flimmer.conf). The sources and their secrets are kept by
// SourceSettings; configured, credentialsReady and secretStoreStatus are
// passed on from there.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON

    Q_PROPERTY(QString streamProfile READ streamProfile NOTIFY changed)
    Q_PROPERTY(int guideDays READ guideDays NOTIFY changed)
    // Empty = Auto (the TV's timezone); otherwise an IANA timezone ID.
    Q_PROPERTY(QString guideTimeZone READ guideTimeZone NOTIFY changed)
    Q_PROPERTY(QString detectedTimeZone READ detectedTimeZone NOTIFY changed)
    Q_PROPERTY(QVariantList guideTimeZones READ guideTimeZones CONSTANT)
    // Seconds behind live: -1 = Auto (grows after stalls), 0 = Off, 2 (default), 5 or 10.
    Q_PROPERTY(int playbackBuffer READ playbackBuffer NOTIFY changed)
    Q_PROPERTY(bool stopInBackground READ stopInBackground NOTIFY changed)
    Q_PROPERTY(bool confirmQuit READ confirmQuit NOTIFY changed)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    Q_PROPERTY(bool credentialsReady READ credentialsReady NOTIFY credentialsReadyChanged)
    Q_PROPERTY(QString secretStoreStatus READ secretStoreStatus NOTIFY secretStoreStatusChanged)
    Q_PROPERTY(QString lastChannel READ lastChannel WRITE setLastChannel NOTIFY lastChannelChanged)
    Q_PROPERTY(QString colorScheme READ colorScheme NOTIFY changed)
    Q_PROPERTY(QVariantList colorSchemes READ colorSchemes CONSTANT)

public:
    static AppSettings *instance();
    static AppSettings *create(QQmlEngine *, QJSEngine *);

    [[nodiscard]] QString streamProfile() const
    {
        return m_streamProfile;
    }

    [[nodiscard]] int guideDays() const
    {
        return m_guideDays;
    }

    [[nodiscard]] QString guideTimeZone() const
    {
        return m_guideTimeZone;
    }

    [[nodiscard]] static QString detectedTimeZone();
    [[nodiscard]] static QVariantList guideTimeZones();
    Q_INVOKABLE [[nodiscard]] QString formatGuideTime(double ms) const;
    Q_INVOKABLE [[nodiscard]] QString formatGuideDay(double ms) const;

    [[nodiscard]] int playbackBuffer() const
    {
        return m_playbackBuffer;
    }

    // Stop the stream while the window isn't focused (and resume afterwards).
    [[nodiscard]] bool stopInBackground() const
    {
        return m_stopInBackground;
    }

    // BACK while watching asks whether to quit instead of returning to the channel list.
    [[nodiscard]] bool confirmQuit() const
    {
        return m_confirmQuit;
    }

    [[nodiscard]] bool configured() const;
    [[nodiscard]] bool credentialsReady() const;
    [[nodiscard]] QString secretStoreStatus() const;
    [[nodiscard]] QString lastChannel() const
    {
        return m_lastChannel;
    }

    [[nodiscard]] QString colorScheme() const
    {
        return m_colorScheme;
    }

    [[nodiscard]] QVariantList colorSchemes() const
    {
        return m_colorSchemes;
    }

    // Applies the configured color scheme to the running application.
    void applyColorScheme() const;

    // Shows a color scheme without storing it; save() makes it permanent and
    // previewColorScheme(colorScheme) reverts to the stored one.
    Q_INVOKABLE static void previewColorScheme(const QString &id);

    void setLastChannel(const QString &uuid);

    // Expected keys: streamProfile, guideDays, guideTimeZone, playbackBuffer, stopInBackground,
    // confirmQuit and
    // colorScheme.
    Q_INVOKABLE void save(const QVariantMap &values);

Q_SIGNALS:
    void changed();
    void credentialsReadyChanged();
    void secretStoreStatusChanged();
    void lastChannelChanged();
    void saved();

private:
    explicit AppSettings(QObject *parent = nullptr);
    const SourceSettings *m_sources;
    QString m_streamProfile;
    int m_guideDays = 3;
    QString m_guideTimeZone;
    int m_playbackBuffer = 2;
    bool m_stopInBackground = true;
    bool m_confirmQuit = true;
    QString m_lastChannel;
    QString m_colorScheme;
    QVariantList m_colorSchemes;
};

#endif // APPSETTINGS_H
