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
    // Seconds behind live: -1 = Auto (grows after stalls), 0 = Off, 2, 5 or 10.
    Q_PROPERTY(int playbackBuffer READ playbackBuffer NOTIFY changed)
    Q_PROPERTY(int rewindMinutes READ rewindMinutes NOTIFY changed)
    Q_PROPERTY(int minFreeGb READ minFreeGb NOTIFY changed)
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

    [[nodiscard]] int playbackBuffer() const
    {
        return m_playbackBuffer;
    }

    // Length of the rewind window; 0 is off.
    [[nodiscard]] int rewindMinutes() const
    {
        return m_rewindMinutes;
    }

    // Rewinding and pausing stop writing to disk below this much free space.
    [[nodiscard]] int minFreeGb() const
    {
        return m_minFreeGb;
    }

    [[nodiscard]] int guideDays() const
    {
        return m_guideDays;
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

    [[nodiscard]] static bool configured();
    [[nodiscard]] static bool credentialsReady();
    [[nodiscard]] static QString secretStoreStatus();

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

    // Expected keys: streamProfile, guideDays, playbackBuffer, rewindMinutes, minFreeGb,
    // stopInBackground, confirmQuit and colorScheme.
    Q_INVOKABLE void save(const QVariantMap &values);

Q_SIGNALS:
    void changed();
    void credentialsReadyChanged();
    void secretStoreStatusChanged();
    void lastChannelChanged();
    void saved();

private:
    explicit AppSettings(QObject *parent = nullptr);
    QString m_streamProfile;
    int m_guideDays = 3;
    int m_playbackBuffer = -1;
    int m_rewindMinutes = 0;
    int m_minFreeGb = 1;
    bool m_stopInBackground = true;
    bool m_confirmQuit = true;
    QString m_lastChannel;
    QString m_colorScheme;
    QVariantList m_colorSchemes;
};

#endif // APPSETTINGS_H
