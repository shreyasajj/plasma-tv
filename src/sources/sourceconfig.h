/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SOURCECONFIG_H
#define SOURCECONFIG_H

#include "serverconfig.h"

#include <QLatin1StringView>
#include <QString>
#include <QVariantMap>

// Settings of one source of channels, stored in the config file. 'secrets'
// (passwords, the MAC address of a Stalker portal, …) are kept apart from the
// 'fields', so they are never handed to QML.
struct SourceConfig {
    // Values of 'type'.
    static constexpr QLatin1StringView Tvheadend{"tvheadend"};
    static constexpr QLatin1StringView M3u{"m3u"};
    static constexpr QLatin1StringView Xtream{"xtream"};
    static constexpr QLatin1StringView Stalker{"stalker"};

    QString id; // "src-3f2a"; prefixes the IDs of the source's channels
    QString type;
    QString name;
    bool enabled = true;
    // Stable number of the source, keeps the IDs of its guide events apart from other sources'.
    int index = 0;
    QVariantMap fields;
    QVariantMap secrets;

    static QString newId();
    // Secret fields of a type; everything else is a plain field.
    static QStringList secretKeys(const QString &type);

    [[nodiscard]] QString field(const QString &key) const
    {
        return fields.value(key).toString();
    }

    [[nodiscard]] QString secret(const QString &key) const
    {
        return secrets.value(key).toString();
    }

    [[nodiscard]] bool isValid() const;

    // TVHeadend and M3U settings in the form their code uses, and back.
    [[nodiscard]] ServerConfig serverConfig() const;
    static SourceConfig fromServer(const ServerConfig &server);

    // Applies the values of the settings page (same keys as the fields, plus "name",
    // "enabled" and secrets only when edited).
    void apply(const QVariantMap &values);

    bool operator==(const SourceConfig &other) const = default;
};

#endif // SOURCECONFIG_H
