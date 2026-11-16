/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef CHANNELIDENTITY_H
#define CHANNELIDENTITY_H

#include <QStringList>

// Automation identities stay separate from legacy channel/guide keys. They never
// depend on stream URLs, credentials, channel numbers or playlist order.
namespace ChannelIdentity
{
// Empty results deliberately exclude ambiguous or unnamed entries from control.
QStringList forPlaylist(const QStringList &explicitIds, const QStringList &names);
}

#endif // CHANNELIDENTITY_H
