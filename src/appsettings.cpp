// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "appsettings.h"

#include "colorschemes.h"
#include "networkauth.h"
#include "streambuffer.h"

#include <QFile>
#include <QQmlEngine>
#include <QSettings>

#include <qt6keychain/keychain.h>

namespace
{
const QString s_keychainService = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("flimmer");
const QString s_keychainKey = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("server-password");
// The password in the config file; older versions kept it in the secret store.
const QString s_passwordKey = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("server/password");

constexpr int32_t kDefaultGuideDays = 14;
constexpr int32_t kDefaultTvhPort = 9981;
constexpr int32_t kMaxPort = 65535;
}

AppSettings *AppSettings::instance()
{
    static auto *s_instance = new AppSettings;
    return s_instance;
}

AppSettings *AppSettings::create(QQmlEngine *, QJSEngine *)
{
    auto *self = instance();
    QQmlEngine::setObjectOwnership(self, QQmlEngine::CppOwnership);
    return self;
}

AppSettings::AppSettings(QObject *parent)
    : QObject(parent)
{
    QSettings settings;

    settings.beginGroup(QStringLiteral("server"));

    m_server.backend =
        ServerConfig::backendFromString(settings.value(QStringLiteral("backend")).toString());

    m_server.host = settings.value(QStringLiteral("host")).toString();
    m_server.port = settings.value(QStringLiteral("port"), kDefaultTvhPort).toInt();
    m_server.https = settings.value(QStringLiteral("https"), false).toBool();
    m_server.username = settings.value(QStringLiteral("username")).toString();
    m_server.playlistUrl = settings.value(QStringLiteral("playlistUrl")).toString();
    m_server.xmltvUrl = settings.value(QStringLiteral("xmltvUrl")).toString();
    settings.endGroup();

    m_streamProfile =
        settings.value(QStringLiteral("playback/profile"), QStringLiteral("pass")).toString();

    m_guideDays =
        qBound(1, settings.value(QStringLiteral("guide/days"), 3).toInt(), kDefaultGuideDays);
    m_playbackBuffer =
        StreamBuffer::normalizeMode(settings.value(QStringLiteral("playback/buffer"), 2).toInt());

    m_stopInBackground = settings.value(QStringLiteral("playback/stopInBackground"), true).toBool();
    m_confirmQuit = settings.value(QStringLiteral("playback/confirmQuit"), true).toBool();
    m_lastChannel = settings.value(QStringLiteral("playback/lastChannel")).toString();

    m_colorScheme =
        settings.value(QStringLiteral("appearance/colorScheme"), ColorSchemes::s_default)
            .toString();
    m_colorSchemes = ColorSchemes::available();

    NetworkAuth::setActiveServer(m_server);
    loadPassword();
}

void AppSettings::setLastChannel(const QString &uuid)
{
    if (uuid == m_lastChannel) {
        return;
    }

    m_lastChannel = uuid;
    QSettings().setValue(QStringLiteral("playback/lastChannel"), uuid);
    Q_EMIT lastChannelChanged();
}

void AppSettings::applyColorScheme() const
{
    ColorSchemes::apply(m_colorScheme);
}

void AppSettings::previewColorScheme(const QString &id)
{
    ColorSchemes::apply(id);
}

void AppSettings::loadPassword()
{
    if (m_server.username.isEmpty()) {
        m_credentialsReady = true;
        return;
    }

    QSettings const settings;

    if (settings.contains(s_passwordKey)) {
        m_server.password = settings.value(s_passwordKey).toString();
        NetworkAuth::setActiveServer(m_server);
        m_credentialsReady = true;
        return;
    }

    // Older versions kept the password in the system secret store. It is moved to the
    // settings once; if the store is locked or unavailable, the move is tried again next time.
    auto *job = new QKeychain::ReadPasswordJob(s_keychainService, this);
    job->setKey(s_keychainKey);
    connect(job, &QKeychain::Job::finished, this, [this, job] {
        if (job->error() == QKeychain::NoError) {
            m_server.password = job->textData();
            storePassword(m_server.password);

            auto *remove = new QKeychain::DeletePasswordJob(s_keychainService, this);
            remove->setKey(s_keychainKey);
            remove->start();
            setSecretStoreStatus({});
        } else if (job->error() != QKeychain::EntryNotFound) {
            qWarning() << "Reading password from secret store failed:" << job->errorString();
            setSecretStoreStatus(tr("Secret store unavailable: %1").arg(job->errorString()));
        }

        NetworkAuth::setActiveServer(m_server);
        m_credentialsReady = true;
        Q_EMIT changed();
        Q_EMIT credentialsReadyChanged();
    });
    job->start();
}

void AppSettings::storePassword(const QString &password)
{
    QSettings settings;

    if (password.isEmpty()) {
        settings.remove(s_passwordKey);
    } else {
        settings.setValue(s_passwordKey, password);
    }

    settings.sync();
    setSecretStoreStatus({});
    // The password is in there: only the user may read the file.
    QFile::setPermissions(settings.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void AppSettings::setSecretStoreStatus(const QString &status)
{
    if (status == m_secretStoreStatus) {
        return;
    }

    m_secretStoreStatus = status;
    Q_EMIT secretStoreStatusChanged();
}

void AppSettings::save(const QVariantMap &values)
{
    const ServerConfig previous = m_server;

    m_server.backend = ServerConfig::backendFromString(
        values.value(QStringLiteral("backend"), backend()).toString());
    m_server.host = values.value(QStringLiteral("host"), m_server.host).toString().trimmed();
    m_server.port =
        qBound(1, values.value(QStringLiteral("port"), m_server.port).toInt(), kMaxPort);
    m_server.https = values.value(QStringLiteral("useHttps"), m_server.https).toBool();
    m_server.username =
        values.value(QStringLiteral("username"), m_server.username).toString().trimmed();
    m_server.playlistUrl =
        values.value(QStringLiteral("playlistUrl"), m_server.playlistUrl).toString().trimmed();
    m_server.xmltvUrl =
        values.value(QStringLiteral("xmltvUrl"), m_server.xmltvUrl).toString().trimmed();

    if (values.contains(QStringLiteral("password"))) {
        m_server.password = values.value(QStringLiteral("password")).toString();
    }

    if (m_server.username.isEmpty()) {
        m_server.password.clear();
    }

    m_streamProfile =
        values.value(QStringLiteral("streamProfile"), m_streamProfile).toString().trimmed();

    if (m_streamProfile.isEmpty()) {
        m_streamProfile = QStringLiteral("pass");
    }

    m_guideDays = qBound(1,
                         values.value(QStringLiteral("guideDays"), m_guideDays).toInt(),
                         kDefaultGuideDays);
    m_playbackBuffer = StreamBuffer::normalizeMode(
        values.value(QStringLiteral("playbackBuffer"), m_playbackBuffer).toInt());
    m_stopInBackground =
        values.value(QStringLiteral("stopInBackground"), m_stopInBackground).toBool();
    m_confirmQuit = values.value(QStringLiteral("confirmQuit"), m_confirmQuit).toBool();
    m_colorScheme = values.value(QStringLiteral("colorScheme"), m_colorScheme).toString();

    QSettings settings;
    settings.beginGroup(QStringLiteral("server"));
    settings.setValue(QStringLiteral("backend"), backend());
    settings.setValue(QStringLiteral("host"), m_server.host);
    settings.setValue(QStringLiteral("port"), m_server.port);
    settings.setValue(QStringLiteral("https"), m_server.https);
    settings.setValue(QStringLiteral("username"), m_server.username);
    settings.setValue(QStringLiteral("playlistUrl"), m_server.playlistUrl);
    settings.setValue(QStringLiteral("xmltvUrl"), m_server.xmltvUrl);
    settings.endGroup();
    settings.setValue(QStringLiteral("playback/profile"), m_streamProfile);
    settings.setValue(QStringLiteral("guide/days"), m_guideDays);
    settings.setValue(QStringLiteral("playback/buffer"), m_playbackBuffer);
    settings.setValue(QStringLiteral("playback/stopInBackground"), m_stopInBackground);
    settings.setValue(QStringLiteral("playback/confirmQuit"), m_confirmQuit);
    settings.setValue(QStringLiteral("appearance/colorScheme"), m_colorScheme);
    applyColorScheme();

    if (m_server.password != previous.password) {
        storePassword(m_server.password);
    }

    NetworkAuth::setActiveServer(m_server);
    Q_EMIT changed();
    Q_EMIT saved(!(m_server == previous));
}
