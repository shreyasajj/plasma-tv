// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sourcesettings.h"

#include "networkauth.h"
#include "secretstore.h"

#include <QFile>
#include <QQmlEngine>
#include <QSettings>

#include <algorithm>

namespace
{
// Settings array of the sources.
constexpr QLatin1StringView s_list{"sources"};
// Settings group of the one server of versions before sources.
constexpr QLatin1StringView s_oldGroup{"server"};
// Secret store key of that server's password before it was kept in the settings.
constexpr QLatin1StringView s_oldSecret{"server-password"};
// Gets the source's prefix when the old server is moved.
constexpr QLatin1StringView s_lastChannel{"playback/lastChannel"};
// Event IDs keep 42 bits for the source's own ID (see ChannelId).
constexpr int s_maxIndex = 1024;

QVariantMap readGroup(QSettings *settings, const QString &group)
{
    QVariantMap values;
    settings->beginGroup(group);

    for (const QString &key : settings->childKeys()) {
        values.insert(key, settings->value(key));
    }

    settings->endGroup();

    return values;
}
}

SourceSettings *SourceSettings::instance()
{
    static SourceSettings *s_instance = [] {
        auto *settings = new QSettings;
        auto *self = new SourceSettings(settings, new KeychainStore);
        settings->setParent(self);

        return self;
    }();

    return s_instance;
}

SourceSettings *SourceSettings::create(QQmlEngine *, QJSEngine *)
{
    auto *self = instance();
    QQmlEngine::setObjectOwnership(self, QQmlEngine::CppOwnership);

    return self;
}

SourceSettings::SourceSettings(QSettings *settings, SecretStore *store, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_store(store)
{
    m_store->setParent(this);
    load();
}

QStringList SourceSettings::types()
{
    return {SourceConfig::Tvheadend,
            SourceConfig::M3u,
            SourceConfig::Xtream,
            SourceConfig::Stalker};
}

QString SourceSettings::typeName(const QString &type)
{
    if (type == SourceConfig::Tvheadend) {
        return tr("TVHeadend");
    }

    if (type == SourceConfig::M3u) {
        return tr("M3U + XMLTV");
    }

    if (type == SourceConfig::Xtream) {
        return tr("Xtream Codes");
    }

    if (type == SourceConfig::Stalker) {
        return tr("Stalker portal");
    }

    return type;
}

void SourceSettings::load()
{
    const int count = m_settings->beginReadArray(s_list);

    for (int i = 0; i < count; ++i) {
        m_settings->setArrayIndex(i);
        SourceConfig config;
        config.id = m_settings->value(QStringLiteral("id")).toString();
        config.type = m_settings->value(QStringLiteral("type")).toString();
        config.name = m_settings->value(QStringLiteral("name")).toString();
        config.enabled = m_settings->value(QStringLiteral("enabled"), true).toBool();
        config.index = m_settings->value(QStringLiteral("index")).toInt();
        config.fields = readGroup(m_settings, QStringLiteral("fields"));
        config.secrets = readGroup(m_settings, QStringLiteral("secrets"));

        if (!config.id.isEmpty() && !config.type.isEmpty()) {
            m_sources.append(config);
        }
    }

    m_settings->endArray();

    // Counts as one pending job until the old server is moved, so it can't finish early.
    m_pending = 1;

    if (m_sources.isEmpty()) {
        migrate();
    }

    pendingDone();
}

bool SourceSettings::migrate()
{
    m_settings->beginGroup(s_oldGroup);
    const bool present = m_settings->contains(QStringLiteral("host"))
        || m_settings->contains(QStringLiteral("playlistUrl"));
    ServerConfig server;
    server.backend =
        ServerConfig::backendFromString(m_settings->value(QStringLiteral("backend")).toString());
    server.host = m_settings->value(QStringLiteral("host")).toString();
    server.port = m_settings->value(QStringLiteral("port"), 9981).toInt();
    server.https = m_settings->value(QStringLiteral("https"), false).toBool();
    server.username = m_settings->value(QStringLiteral("username")).toString();
    server.playlistUrl = m_settings->value(QStringLiteral("playlistUrl")).toString();
    server.xmltvUrl = m_settings->value(QStringLiteral("xmltvUrl")).toString();
    const bool hasPassword = m_settings->contains(QStringLiteral("password"));
    server.password = m_settings->value(QStringLiteral("password")).toString();
    m_settings->endGroup();

    if (!present) {
        return false;
    }

    if (!server.isValid()) {
        removeOldSettings();

        return false;
    }

    SourceConfig config = SourceConfig::fromServer(server);
    config.id = uniqueId();
    config.index = 0;
    m_sources = {config};

    // The last channel now carries the source in its ID.
    const QString last = m_settings->value(s_lastChannel).toString();

    if (!last.isEmpty() && !last.contains(QLatin1Char(':'))) {
        m_settings->setValue(s_lastChannel, config.id + QLatin1Char(':') + last);
    }

    // Newer versions of the single server kept the password in the settings already.
    if (server.username.isEmpty() || hasPassword) {
        persist();
        removeOldSettings();

        return true;
    }

    // Saved only once the password is known, so a locked secret store is tried again next time.
    const QString id = config.id;
    ++m_pending;
    m_store->read(s_oldSecret, [this, id](const SecretStore::Result &read) {
        if (!read.ok) {
            qWarning() << "Reading the password from the secret store failed:" << read.error;
            setSecretStoreStatus(tr("Secret store unavailable: %1").arg(read.error));
            pendingDone();

            return;
        }

        const int index = indexOf(id);

        if (read.found && index >= 0) {
            m_sources[index].secrets.insert(QStringLiteral("password"), read.value);
            m_store->remove(s_oldSecret, [](const SecretStore::Result &) { });
        }

        persist();
        removeOldSettings();
        pendingDone();
    });

    return true;
}

void SourceSettings::pendingDone()
{
    if (--m_pending > 0 || m_credentialsReady) {
        return;
    }

    m_credentialsReady = true;
    updateNetworkAuth();
    Q_EMIT changed();
    Q_EMIT credentialsReadyChanged();
}

void SourceSettings::persist()
{
    m_settings->remove(s_list);
    m_settings->beginWriteArray(s_list, static_cast<int>(m_sources.size()));

    for (int i = 0; i < m_sources.size(); ++i) {
        const SourceConfig &config = m_sources.at(i);
        m_settings->setArrayIndex(i);
        m_settings->setValue(QStringLiteral("id"), config.id);
        m_settings->setValue(QStringLiteral("type"), config.type);
        m_settings->setValue(QStringLiteral("name"), config.name);
        m_settings->setValue(QStringLiteral("enabled"), config.enabled);
        m_settings->setValue(QStringLiteral("index"), config.index);

        for (auto it = config.fields.cbegin(); it != config.fields.cend(); ++it) {
            m_settings->setValue(QStringLiteral("fields/") + it.key(), it.value());
        }

        for (auto it = config.secrets.cbegin(); it != config.secrets.cend(); ++it) {
            m_settings->setValue(QStringLiteral("secrets/") + it.key(), it.value());
        }
    }

    m_settings->endArray();
    m_settings->sync();
    // Passwords are in there: only the user may read the file.
    QFile::setPermissions(m_settings->fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void SourceSettings::removeOldSettings()
{
    m_settings->remove(s_oldGroup);
}

SourceConfig SourceSettings::config(const QString &id) const
{
    const int index = indexOf(id);

    return index >= 0 ? m_sources.at(index) : SourceConfig();
}

QVariantList SourceSettings::sources() const
{
    QVariantList list;

    for (const SourceConfig &config : m_sources) {
        list.append(QVariantMap{{QStringLiteral("id"), config.id},
                                {QStringLiteral("type"), config.type},
                                {QStringLiteral("name"), config.name},
                                {QStringLiteral("enabled"), config.enabled}});
    }

    return list;
}

bool SourceSettings::configured() const
{
    return std::ranges::any_of(m_sources, [](const SourceConfig &config) {
        return config.enabled;
    });
}

QVariantMap SourceSettings::source(const QString &id) const
{
    const SourceConfig config = this->config(id);
    QVariantMap map = config.fields;
    map.insert(QStringLiteral("id"), config.id);
    map.insert(QStringLiteral("type"), config.type);
    map.insert(QStringLiteral("name"), config.name);
    map.insert(QStringLiteral("enabled"), config.enabled);

    for (const QString &key : SourceConfig::secretKeys(config.type)) {
        map.insert(key + QStringLiteral("Set"), config.secrets.contains(key));
    }

    return map;
}

SourceConfig SourceSettings::draft(const QVariantMap &values) const
{
    SourceConfig config = this->config(values.value(QStringLiteral("id")).toString());

    if (config.id.isEmpty()) {
        config.id = uniqueId();
    }

    config.apply(values);

    return config;
}

bool SourceSettings::valid(const QVariantMap &values) const
{
    return draft(values).isValid();
}

QString SourceSettings::save(const QVariantMap &values)
{
    QString id = values.value(QStringLiteral("id")).toString();
    int index = indexOf(id);

    if (index < 0) {
        SourceConfig config;
        config.id = uniqueId();
        config.index = freeIndex();
        m_sources.append(config);
        index = static_cast<int>(m_sources.size()) - 1;
        id = config.id;
    }

    SourceConfig &config = m_sources[index];
    config.apply(values);

    if (config.name.isEmpty()) {
        config.name = config.type;
    }

    persist();
    // The old single server is replaced for good once anything is saved.
    removeOldSettings();
    updateNetworkAuth();
    Q_EMIT changed();
    Q_EMIT saved(id);

    return id;
}

void SourceSettings::remove(const QString &id)
{
    const int index = indexOf(id);

    if (index < 0) {
        return;
    }

    m_sources.removeAt(index);
    persist();
    updateNetworkAuth();
    Q_EMIT changed();
    Q_EMIT removed(id);
}

void SourceSettings::move(const QString &id, int delta)
{
    const int index = indexOf(id);
    const int target = index + delta;

    if (index < 0 || target < 0 || target >= m_sources.size()) {
        return;
    }

    m_sources.move(index, target);
    persist();
    Q_EMIT changed();
    Q_EMIT reordered();
}

void SourceSettings::updateNetworkAuth()
{
    QList<NetworkAuth::HostCredentials> credentials;

    for (const SourceConfig &config : std::as_const(m_sources)) {
        if (!config.enabled
            || (config.type != SourceConfig::Tvheadend && config.type != SourceConfig::M3u)) {
            continue;
        }

        const ServerConfig server = config.serverConfig();

        if (!server.username.isEmpty()) {
            credentials.append({.host = server.baseUrl().host(),
                                .username = server.username,
                                .password = server.password});
        }
    }

    NetworkAuth::setCredentials(credentials);
}

void SourceSettings::setSecretStoreStatus(const QString &status)
{
    if (status == m_secretStoreStatus) {
        return;
    }

    m_secretStoreStatus = status;
    Q_EMIT secretStoreStatusChanged();
}

int SourceSettings::indexOf(const QString &id) const
{
    for (int i = 0; i < m_sources.size(); ++i) {
        if (m_sources.at(i).id == id) {
            return i;
        }
    }

    return -1;
}

QString SourceSettings::uniqueId() const
{
    QString id;

    do {
        id = SourceConfig::newId();
    } while (indexOf(id) >= 0);

    return id;
}

int SourceSettings::freeIndex() const
{
    for (int index = 0; index < s_maxIndex; ++index) {
        const bool used = std::ranges::any_of(m_sources, [index](const SourceConfig &config) {
            return config.index == index;
        });

        if (!used) {
            return index;
        }
    }

    return s_maxIndex - 1;
}
