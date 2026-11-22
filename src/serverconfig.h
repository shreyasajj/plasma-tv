/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>
#include <QUrl>

// Connection parameters for a TVHeadend server or an M3U playlist with an
// XMLTV guide (e.g. Dispatcharr), as SourceConfig hands them to the TVHeadend
// and M3U code.
struct ServerConfig {
    enum class Backend : std::uint8_t {
        Tvheadend,
        M3u,
    };

    Backend backend = Backend::Tvheadend;

    // TVHeadend
    QString host;
    int port = 9981;
    bool https = false;
    QString username;
    QString password;

    // M3U + XMLTV; an empty guide URL means the one announced by the playlist.
    QString playlistUrl;
    QString xmltvUrl;

    static Backend backendFromString(const QString &name);
    static QString backendName(Backend backend);

    [[nodiscard]] bool isValid() const;
    [[nodiscard]] QUrl playlist() const;
    [[nodiscard]] QUrl xmltv() const;

    // Base URL including an optional web root, always ending in '/'. For M3U
    // sources this is the root of the playlist's server.
    [[nodiscard]] QUrl baseUrl() const;

    // Resolves a server relative path (e.g. "api/serverinfo") against baseUrl().
    [[nodiscard]] QUrl url(const QString &path) const;

    // Identifies the server (not the user) so caches of another server can be discarded.
    [[nodiscard]] QString identity() const;

    bool operator==(const ServerConfig &other) const = default;
};

#endif // SERVERCONFIG_H
