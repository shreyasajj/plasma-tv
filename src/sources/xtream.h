/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XTREAM_H
#define XTREAM_H

#include "epgstore.h"
#include "xmltv.h"

#include <QByteArray>
#include <QStringList>
#include <QUrl>
#include <QVector>

// The Xtream Codes player API (player_api.php): addresses and replies.
// Pure functions, so they can be tested with recorded replies.
namespace Xtream
{
struct Account {
    bool auth = false;
    QString status; // "Active", "Expired", "Banned", "Disabled", …
    qint64 expires = 0; // seconds since epoch, 0 = never
    int maxConnections = 0; // 0 = no limit
    int activeConnections = 0;
    QStringList formats; // allowed_output_formats
    QString message;
};

struct Streams {
    QVector<Channel> channels; // IDs are stream_id
    Xmltv::ChannelMap guideMap; // epg_channel_id and names → stream_id
};

// "panel.example:8080" or "http://panel.example:8080/" → "http://panel.example:8080/".
QUrl baseUrl(const QString &server);
QUrl apiUrl(const QUrl &base,
            const QString &user,
            const QString &password,
            const QString &action = {});
QUrl guideUrl(const QUrl &base, const QString &user, const QString &password);
// MPEG-TS unless the account only allows HLS.
QUrl streamUrl(const QUrl &base,
               const QString &user,
               const QString &password,
               const QString &streamId,
               const QStringList &formats);

// False when the reply isn't a login reply at all.
bool parseLogin(const QByteArray &data, Account *account);
// Why the account can't be used, or an empty string.
QString loginError(const Account &account, qint64 now);
bool inUse(const Account &account);
// False when the reply isn't a list of streams.
bool parseStreams(const QByteArray &data, Streams *streams);
}

#endif // XTREAM_H
