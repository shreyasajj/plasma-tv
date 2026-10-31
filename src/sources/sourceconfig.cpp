// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "sourceconfig.h"

#include <QRandomGenerator>
#include <QUrl>

QString SourceConfig::newId()
{
    return QStringLiteral("src-%1").arg(QRandomGenerator::global()->bounded(0x10000),
                                        4,
                                        16,
                                        QLatin1Char('0'));
}

QStringList SourceConfig::secretKeys(const QString &type)
{
    if (type == Stalker) {
        return {QStringLiteral("mac"),
                QStringLiteral("serial"),
                QStringLiteral("deviceId"),
                QStringLiteral("deviceId2"),
                QStringLiteral("signature"),
                QStringLiteral("password")};
    }

    return {QStringLiteral("password")};
}

bool SourceConfig::isValid() const
{
    if (type == Tvheadend || type == M3u) {
        return serverConfig().isValid();
    }

    if (type == Xtream) {
        return QUrl::fromUserInput(field(QStringLiteral("server"))).isValid()
            && !field(QStringLiteral("server")).trimmed().isEmpty()
            && !field(QStringLiteral("username")).isEmpty();
    }

    if (type == Stalker) {
        return !field(QStringLiteral("portal")).trimmed().isEmpty()
            && !secret(QStringLiteral("mac")).isEmpty();
    }

    return false;
}

ServerConfig SourceConfig::serverConfig() const
{
    ServerConfig server;
    server.backend = type == M3u ? ServerConfig::Backend::M3u : ServerConfig::Backend::Tvheadend;
    server.host = field(QStringLiteral("host"));
    server.port = fields.value(QStringLiteral("port"), 9981).toInt();
    server.https = fields.value(QStringLiteral("https")).toBool();
    server.username = field(QStringLiteral("username"));
    server.password = secret(QStringLiteral("password"));
    server.playlistUrl = field(QStringLiteral("playlistUrl"));
    server.xmltvUrl = field(QStringLiteral("xmltvUrl"));

    return server;
}

SourceConfig SourceConfig::fromServer(const ServerConfig &server)
{
    SourceConfig config;
    const bool m3u = server.backend == ServerConfig::Backend::M3u;
    config.type = m3u ? M3u : Tvheadend;

    if (m3u) {
        config.fields.insert(QStringLiteral("playlistUrl"), server.playlistUrl);
        config.fields.insert(QStringLiteral("xmltvUrl"), server.xmltvUrl);
    } else {
        config.fields.insert(QStringLiteral("host"), server.host);
        config.fields.insert(QStringLiteral("port"), server.port);
        config.fields.insert(QStringLiteral("https"), server.https);
    }

    config.fields.insert(QStringLiteral("username"), server.username);

    if (!server.password.isEmpty()) {
        config.secrets.insert(QStringLiteral("password"), server.password);
    }

    const QUrl base = server.baseUrl();
    config.name = m3u ? QObject::tr("M3U playlist") : base.host();

    if (config.name.isEmpty()) {
        config.name = QStringLiteral("TVHeadend");
    }

    return config;
}

void SourceConfig::apply(const QVariantMap &values)
{
    const QStringList secretList =
        secretKeys(values.value(QStringLiteral("type"), type).toString());

    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QString &key = it.key();

        if (key == QLatin1String("id") || key == QLatin1String("index")) {
            continue;
        }

        if (key == QLatin1String("type")) {
            type = it.value().toString();
        } else if (key == QLatin1String("name")) {
            name = it.value().toString().trimmed();
        } else if (key == QLatin1String("enabled")) {
            enabled = it.value().toBool();
        } else if (secretList.contains(key)) {
            const QString value = it.value().toString();

            if (value.isEmpty()) {
                secrets.remove(key);
            } else {
                secrets.insert(key, value);
            }
        } else {
            const QVariant &value = it.value();
            fields.insert(key,
                          value.typeId() == QMetaType::QString
                              ? QVariant(value.toString().trimmed())
                              : value);
        }
    }

    // Like today: without a username there is no password.
    if ((type == Tvheadend || type == M3u || type == Xtream)
        && field(QStringLiteral("username")).isEmpty()) {
        secrets.remove(QStringLiteral("password"));
    }
}
