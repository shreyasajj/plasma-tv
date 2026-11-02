/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SOURCECONFIG_H
#define SOURCECONFIG_H

#include "serverconfig.h"

#include <QString>
#include <QVariantMap>

// Settings of one source of channels. 'fields' are stored in the config
// file; 'secrets' (passwords, the MAC address of a Stalker portal, …) only
// live in memory and go to the system secret store.
struct SourceConfig {
    static inline const QString Tvheadend = // NOLINT(bugprone-throwing-static-initialization)
        QStringLiteral("tvheadend");
    static inline const QString M3u = // NOLINT(bugprone-throwing-static-initialization)
        QStringLiteral("m3u");
    static inline const QString Xtream = // NOLINT(bugprone-throwing-static-initialization)
        QStringLiteral("xtream");
    static inline const QString Stalker = // NOLINT(bugprone-throwing-static-initialization)
        QStringLiteral("stalker");

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
