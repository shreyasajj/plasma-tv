/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include "serverconfig.h"

#include <QObject>
#include <QVariantMap>
#include <qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

// Persistent application settings. Non-secret values go to the regular
// config file (~/.config/flimmer/flimmer.conf), the
// password is stored in the system secret store (KWallet / Secret Service)
// through QtKeychain and is never exposed to QML in plain text.
class AppSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Settings)
    QML_SINGLETON

    // "tvheadend" or "m3u"
    Q_PROPERTY(QString backend READ backend NOTIFY changed)
    Q_PROPERTY(QString host READ host NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
    Q_PROPERTY(bool useHttps READ useHttps NOTIFY changed)
    Q_PROPERTY(QString username READ username NOTIFY changed)
    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY changed)
    Q_PROPERTY(QString playlistUrl READ playlistUrl NOTIFY changed)
    Q_PROPERTY(QString xmltvUrl READ xmltvUrl NOTIFY changed)
    Q_PROPERTY(QString streamProfile READ streamProfile NOTIFY changed)
    Q_PROPERTY(int guideDays READ guideDays NOTIFY changed)
    // Seconds behind live: -1 = Auto (grows after stalls), 0 = Off, 2, 5 or 10.
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

    [[nodiscard]] QString backend() const
    {
        return ServerConfig::backendName(m_server.backend);
    }

    [[nodiscard]] QString host() const
    {
        return m_server.host;
    }

    [[nodiscard]] int port() const
    {
        return m_server.port;
    }

    [[nodiscard]] bool useHttps() const
    {
        return m_server.https;
    }

    [[nodiscard]] QString username() const
    {
        return m_server.username;
    }

    [[nodiscard]] bool hasPassword() const
    {
        return !m_server.password.isEmpty();
    }

    [[nodiscard]] QString playlistUrl() const
    {
        return m_server.playlistUrl;
    }

    [[nodiscard]] QString xmltvUrl() const
    {
        return m_server.xmltvUrl;
    }

    [[nodiscard]] QString streamProfile() const
    {
        return m_streamProfile;
    }

    [[nodiscard]] int playbackBuffer() const
    {
        return m_playbackBuffer;
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

    [[nodiscard]] bool configured() const
    {
        return m_server.isValid();
    }

    [[nodiscard]] bool credentialsReady() const
    {
        return m_credentialsReady;
    }

    [[nodiscard]] QString secretStoreStatus() const
    {
        return m_secretStoreStatus;
    }

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

    [[nodiscard]] ServerConfig server() const
    {
        return m_server;
    }

    // Expected keys: backend, host, port, useHttps, playlistUrl, xmltvUrl, username, streamProfile,
    // guideDays, playbackBuffer, stopInBackground, confirmQuit, colorScheme and, only when the
    // password was edited, password.
    Q_INVOKABLE void save(const QVariantMap &values);

Q_SIGNALS:
    void changed();
    void credentialsReadyChanged();
    void secretStoreStatusChanged();
    void lastChannelChanged();
    // Emitted after save(); serverChanged is true when connection parameters differ.
    void saved(bool serverChanged);

private:
    explicit AppSettings(QObject *parent = nullptr);
    void loadPassword();
    void storePassword(const QString &password);
    void setSecretStoreStatus(const QString &status);

    ServerConfig m_server;
    QString m_streamProfile;
    int m_guideDays = 3;
    int m_playbackBuffer = -1;
    bool m_stopInBackground = true;
    bool m_confirmQuit = true;
    QString m_lastChannel;
    QString m_colorScheme;
    QVariantList m_colorSchemes;
    bool m_credentialsReady = false;
    QString m_secretStoreStatus;
};

#endif // APPSETTINGS_H
