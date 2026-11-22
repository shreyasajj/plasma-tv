// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "appsettings.h"

#include "colorschemes.h"
#include "sources/sourcesettings.h"
#include "streambuffer.h"

#include <QQmlEngine>
#include <QSettings>

namespace
{
constexpr int32_t kDefaultGuideDays = 14;
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
    , m_sources(SourceSettings::instance())
{
    // Moves the server of older versions into the sources first, which also updates the
    // last channel's ID.
    connect(m_sources, &SourceSettings::changed, this, &AppSettings::changed);
    connect(m_sources,
            &SourceSettings::credentialsReadyChanged,
            this,
            &AppSettings::credentialsReadyChanged);
    connect(m_sources,
            &SourceSettings::secretStoreStatusChanged,
            this,
            &AppSettings::secretStoreStatusChanged);

    QSettings const settings;
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
}

bool AppSettings::configured() const
{
    return m_sources->configured();
}

bool AppSettings::credentialsReady() const
{
    return m_sources->credentialsReady();
}

QString AppSettings::secretStoreStatus() const
{
    return m_sources->secretStoreStatus();
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

void AppSettings::save(const QVariantMap &values)
{
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
    settings.setValue(QStringLiteral("playback/profile"), m_streamProfile);
    settings.setValue(QStringLiteral("guide/days"), m_guideDays);
    settings.setValue(QStringLiteral("playback/buffer"), m_playbackBuffer);
    settings.setValue(QStringLiteral("playback/stopInBackground"), m_stopInBackground);
    settings.setValue(QStringLiteral("playback/confirmQuit"), m_confirmQuit);
    settings.setValue(QStringLiteral("appearance/colorScheme"), m_colorScheme);
    applyColorScheme();

    Q_EMIT changed();
    Q_EMIT saved();
}
