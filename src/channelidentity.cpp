// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "channelidentity.h"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>

QStringList ChannelIdentity::forPlaylist(const QStringList &explicitIds, const QStringList &names)
{
    if (explicitIds.size() != names.size()) {
        return {};
    }

    QHash<QString, int> counts;

    for (const QString &id : explicitIds) {
        ++counts[id];
    }

    QStringList keys;
    QHash<QString, int> keyCounts;

    for (qsizetype i = 0; i < explicitIds.size(); ++i) {
        const QString &id = explicitIds.at(i);
        const QString &name = names.at(i);
        const bool uniqueId = !id.isEmpty() && counts.value(id) == 1;
        const QJsonArray identity{QStringLiteral("m3u-v1"), id, uniqueId ? QString() : name};
        const QString key = !uniqueId && name.isEmpty()
            ? QString()
            : QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
        keys.append(key);
        ++keyCounts[key];
    }

    QStringList result;

    for (const QString &key : keys) {
        if (key.isEmpty() || keyCounts.value(key) != 1) {
            result.append(QString());
            continue;
        }

        result.append(
            QStringLiteral("m3u-")
            + QString::fromLatin1(
                QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex()));
    }

    return result;
}
