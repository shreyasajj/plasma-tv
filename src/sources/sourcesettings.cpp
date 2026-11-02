// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sourcesettings.h"

#include "networkauth.h"
#include "secretstore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlEngine>
#include <QSettings>
#include <algorithm>

namespace
{
const QString s_list = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("sources");
const QString s_oldGroup = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("server");
const QString s_oldSecret = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("server-password");
const QString s_lastChannel = // NOLINT(bugprone-throwing-static-initialization)
    QStringLiteral("playback/lastChannel");
// Event IDs keep 42 bits for the source's own ID (see ChannelId).
constexpr int s_maxIndex = 1024;

QString secretKey(const QString &id)
{
    return QStringLiteral("source/%1/secret").arg(id);
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
        m_settings->beginGroup(QStringLiteral("fields"));

        for (const QString &key : m_settings->childKeys()) {
            config.fields.insert(key, m_settings->value(key));
        }

        m_settings->endGroup();

        if (!config.id.isEmpty() && !config.type.isEmpty()) {
            m_sources.append(config);
        }
    }

    m_settings->endArray();

    // Counts as one pending job until everything is started, so it can't finish early.
    m_pending = 1;

    if (m_sources.isEmpty()) {
        migrate();
    } else {
        loadSecrets();
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

    if (server.username.isEmpty()) {
        persist();
        removeOldSettings();
        return true;
    }

    // Saved only once the password moved, so a failed write is tried again next time.
    const QString id = config.id;
    ++m_pending;
    m_store->read(s_oldSecret, [this, id](const SecretStore::Result &read) {
        const int index = indexOf(id);

        if (!read.ok) {
            setSecretStoreStatus(tr("Secret store unavailable: %1").arg(read.error));
            pendingDone();
            return;
        }

        if (!read.found || index < 0) {
            persist();
            removeOldSettings();
            pendingDone();
            return;
        }

        m_sources[index].secrets.insert(QStringLiteral("password"), read.value);
        const QString json = QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(m_sources.at(index).secrets))
                .toJson(QJsonDocument::Compact));

        m_store->write(secretKey(id), json, [this](const SecretStore::Result &write) {
            if (write.ok) {
                persist();
                removeOldSettings();
                m_store->remove(s_oldSecret, [](const SecretStore::Result &) { });
            } else {
                setSecretStoreStatus(tr("Secret store unavailable: %1").arg(write.error));
            }

            pendingDone();
        });
    });

    return true;
}

void SourceSettings::loadSecrets()
{
    for (const SourceConfig &config : std::as_const(m_sources)) {
        const QString id = config.id;
        ++m_pending;
        m_store->read(secretKey(id), [this, id](const SecretStore::Result &result) {
            const int index = indexOf(id);

            if (!result.ok) {
                qWarning() << "Reading secrets from secret store failed:" << result.error;
                setSecretStoreStatus(tr("Secret store unavailable: %1").arg(result.error));
            } else if (result.found && index >= 0) {
                m_sources[index].secrets =
                    QJsonDocument::fromJson(result.value.toUtf8()).object().toVariantMap();
            }

            pendingDone();
        });
    }
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

void SourceSettings::storeSecrets(const SourceConfig &config)
{
    const auto done = [this](const SecretStore::Result &result) {
        if (result.ok) {
            setSecretStoreStatus({});
        } else {
            qWarning() << "Storing secrets in secret store failed:" << result.error;
            setSecretStoreStatus(
                tr("Password could not be stored securely and is kept for this session only (%1)")
                    .arg(result.error));
        }
    };

    if (config.secrets.isEmpty()) {
        m_store->remove(secretKey(config.id), done);
        return;
    }

    m_store->write(secretKey(config.id),
                   QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(config.secrets))
                                         .toJson(QJsonDocument::Compact)),
                   done);
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
    }

    m_settings->endArray();
    m_settings->sync();
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
    const QVariantMap previousSecrets = config.secrets;
    config.apply(values);

    if (config.name.isEmpty()) {
        config.name = config.type;
    }

    persist();
    // The old single server is replaced for good once anything is saved.
    removeOldSettings();

    if (config.secrets != previousSecrets) {
        storeSecrets(config);
    }

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
    m_store->remove(secretKey(id), [](const SecretStore::Result &) { });
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
