/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STALKER_H
#define STALKER_H

#include "epgstore.h"
#include "headers.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QUrl>
#include <QVector>

// The Stalker (Ministra) portal API that MAG set-top boxes use: addresses,
// headers and replies. Pure functions, so they can be tested with recorded
// replies.
namespace Stalker
{
// What the portal knows the box by. Only the MAC address is required; empty
// fields are left out of every request.
struct Device {
    QString mac;
    QString model = QStringLiteral("MAG250");
    QString serial;
    QString deviceId;
    QString deviceId2;
    QString signature;
    QString login;
    QString password;
    QString timezone;
};

// "00:1a:79:aa:bb:cc", "001A79AABBCC", … → "00:1A:79:AA:BB:CC"; empty if it isn't one.
QString normalizeMac(const QString &mac);

// The portal's web address ("http://portal.example/c/") → its root ("http://portal.example/").
QUrl portalRoot(const QString &address);
// The API addresses portals use, in the order they are tried.
QList<QUrl> endpoints(const QUrl &root);

// Sent with every request; 'token' once the handshake gave one.
Headers headers(const Device &device, const QUrl &root, const QString &token = {});
// Sent with the stream: some portals check the box there as well.
Headers streamHeaders(const Device &device, const QUrl &root);

QUrl handshakeUrl(const QUrl &endpoint);
QUrl profileUrl(const QUrl &endpoint, const Device &device);
QUrl authUrl(const QUrl &endpoint, const Device &device);
QUrl channelsUrl(const QUrl &endpoint);
QUrl guideUrl(const QUrl &endpoint, int hours);
QUrl linkUrl(const QUrl &endpoint, const QString &cmd);
QUrl watchdogUrl(const QUrl &endpoint);

// The portal no longer accepts the token (or never did).
bool isAuthFailure(int httpStatus, const QByteArray &data);
// Empty when the reply has no token.
QString parseToken(const QByteArray &data);
// False when the reply isn't from a portal at all.
bool parseProfile(const QByteArray &data);
bool parseAuth(const QByteArray &data);
// Channel IDs are the portal's; 'stream' holds the channel's cmd, which create_link needs.
bool parseChannels(const QByteArray &data, const QUrl &root, QVector<Channel> *channels);
bool parseGuide(const QByteArray &data, QVector<EpgEvent> *events);
// The playable address of a create_link reply, without the "ffmpeg " prefix.
QUrl parseLink(const QByteArray &data);
}

#endif // STALKER_H
