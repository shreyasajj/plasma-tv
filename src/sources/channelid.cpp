// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "channelid.h"

#include <QSet>

#include <algorithm>

namespace
{
// The bits of an event ID that hold the source's own event ID.
constexpr qint64 s_eventMask = (static_cast<qint64>(1) << ChannelId::s_eventBits) - 1;
}

QString ChannelId::make(const QString &sourceId, const QString &ownId)
{
    return sourceId + QLatin1Char(':') + ownId;
}

QString ChannelId::sourceOf(const QString &channelId)
{
    const qsizetype colon = channelId.indexOf(QLatin1Char(':'));

    return colon < 0 ? QString() : channelId.left(colon);
}

QString ChannelId::ownOf(const QString &channelId)
{
    const qsizetype colon = channelId.indexOf(QLatin1Char(':'));

    return colon < 0 ? channelId : channelId.mid(colon + 1);
}

qint64 ChannelId::makeEvent(int sourceIndex, qint64 ownId)
{
    return (static_cast<qint64>(sourceIndex) << s_eventBits) | (ownId & s_eventMask);
}

int ChannelId::sourceIndexOf(qint64 eventId)
{
    return static_cast<int>(eventId >> s_eventBits);
}

qint64 ChannelId::ownEventOf(qint64 eventId)
{
    return eventId & s_eventMask;
}

QVector<Channel> ChannelId::prefixed(const QString &sourceId, QVector<Channel> channels)
{
    for (Channel &channel : channels) {
        channel.uuid = make(sourceId, channel.uuid);
    }

    return channels;
}

QVector<EpgEvent>
ChannelId::prefixed(const QString &sourceId, int sourceIndex, QVector<EpgEvent> events)
{
    for (EpgEvent &event : events) {
        event.id = makeEvent(sourceIndex, event.id);
        event.channel = make(sourceId, event.channel);
    }

    return events;
}

QVector<Channel> ChannelId::merge(const QList<QVector<Channel>> &sources)
{
    if (sources.size() == 1) {
        return sources.first();
    }

    QVector<Channel> result;
    QSet<int> used;
    int highest = 0;

    for (const QVector<Channel> &channels : sources) {
        for (const Channel &channel : channels) {
            highest = std::max(highest, channel.number);
        }
    }

    for (const QVector<Channel> &channels : sources) {
        // Only numbers of earlier sources count; a source's own duplicates stay as they are.
        QSet<int> own;

        for (Channel channel : channels) {
            if (channel.number > 0 && used.contains(channel.number)) {
                channel.number = ++highest;
            }

            if (channel.number > 0) {
                own.insert(channel.number);
            }

            result.append(channel);
        }

        used.unite(own);
    }

    // Unnumbered channels go to the end, as within one source.
    std::ranges::stable_sort(result, [](const Channel &a, const Channel &b) {
        if ((a.number == 0) != (b.number == 0)) {
            return a.number != 0;
        }

        return a.number < b.number;
    });

    return result;
}
