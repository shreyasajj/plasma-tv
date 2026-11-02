/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef CHANNELID_H
#define CHANNELID_H

#include "epgstore.h"

#include <QList>
#include <QString>
#include <QVector>

// Keeps the channels and guide events of several sources apart: channel IDs
// become "<source id>:<the source's own ID>", event IDs get the source's index
// in their upper bits.
namespace ChannelId
{
// Bits left for a source's own event IDs; TVHeadend's are far smaller.
constexpr int s_eventBits = 42;

QString make(const QString &sourceId, const QString &ownId);
QString sourceOf(const QString &channelId);
QString ownOf(const QString &channelId);

qint64 makeEvent(int sourceIndex, qint64 ownId);
int sourceIndexOf(qint64 eventId);
qint64 ownEventOf(qint64 eventId);

// Gives channels and events of one source their prefixed IDs.
QVector<Channel> prefixed(const QString &sourceId, QVector<Channel> channels);
QVector<EpgEvent> prefixed(const QString &sourceId, int sourceIndex, QVector<EpgEvent> events);

// One list of the sources' channels, in source order. A number already used by an earlier
// source becomes the next free number after the highest one; unnumbered channels stay so.
QVector<Channel> merge(const QList<QVector<Channel>> &sources);
}

#endif // CHANNELID_H
