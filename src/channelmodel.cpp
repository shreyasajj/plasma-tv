// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "channelmodel.h"

#include <QDateTime>

ChannelModel::ChannelModel(const EpgStore *store, QObject *parent)
    : QAbstractListModel(parent)
    , m_store(store)
{
}

int ChannelModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_channels.size());
}

QHash<int, QByteArray> ChannelModel::roleNames() const
{
    static const QHash<int, QByteArray> roles = {
        {UuidRole, "uuid"},
        {NumberRole, "number"},
        {NameRole, "name"},
        {IconUrlRole, "iconUrl"},
        {NowIdRole, "nowId"},
        {NowTitleRole, "nowTitle"},
        {NowSubtitleRole, "nowSubtitle"},
        {NowSummaryRole, "nowSummary"},
        {NowStartRole, "nowStart"},
        {NowStopRole, "nowStop"},
        {NowProgressRole, "nowProgress"},
        {NowGenreRole, "nowGenre"},
        {NowRecordingRole, "nowRecording"},
        {NextTitleRole, "nextTitle"},
        {NextStartRole, "nextStart"},
    };

    return roles;
}

QVariant ChannelModel::data(const QModelIndex &index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid)) {
        return {};
    }
    const Channel &channel = m_channels.at(index.row());
    const NowNext &nowNext = m_nowNext.at(index.row());

    switch (role) {
    case UuidRole:
        return channel.uuid;
    case NumberRole:
        return channel.number;
    case NameRole:
        return channel.name;
    case IconUrlRole:
        return iconUrl(index.row());
    case NowIdRole:
        return nowNext.now ? QVariant(nowNext.now->id) : QVariant(-1);
    case NowTitleRole:
        return nowNext.now ? nowNext.now->title : QString();
    case NowSubtitleRole:
        return nowNext.now ? nowNext.now->subtitle : QString();
    case NowSummaryRole:
        if (!nowNext.now) {
            return QString();
        }
        return nowNext.now->summary.isEmpty() ? nowNext.now->description : nowNext.now->summary;
    case NowStartRole:
        return nowNext.now ? static_cast<double>(nowNext.now->start) * 1000.0 : 0.0;
    case NowStopRole:
        return nowNext.now ? static_cast<double>(nowNext.now->stop) * 1000.0 : 0.0;
    case NowProgressRole: {
        if (!nowNext.now) {
            return 0.0;
        }
        const auto now = static_cast<double>(QDateTime::currentSecsSinceEpoch());
        return qBound(0.0,
                      (now - static_cast<double>(nowNext.now->start))
                          / static_cast<double>(nowNext.now->stop - nowNext.now->start),
                      1.0);
    }
    case NowGenreRole:
        return nowNext.now ? (nowNext.now->genre & 0xF0U) : 0;
    case NowRecordingRole:
        return nowNext.now && nowNext.now->dvrState == QLatin1String("recording");
    case NextTitleRole:
        return nowNext.next ? nowNext.next->title : QString();
    case NextStartRole:
        return nowNext.next ? static_cast<double>(nowNext.next->start) * 1000.0 : 0.0;
    default:
        return {};
    }
}

void ChannelModel::setChannels(const QVector<Channel> &channels)
{
    const bool countChange = channels.size() != m_channels.size();
    beginResetModel();
    m_channels = channels;
    m_nowNext = QList<NowNext>(m_channels.size());
    m_rowOfUuid.clear();

    for (int i = 0; i < m_channels.size(); ++i) {
        m_rowOfUuid.insert(m_channels.at(i).uuid, i);
    }

    endResetModel();
    refreshNow();

    if (countChange) {
        Q_EMIT countChanged();
    }
}

void ChannelModel::refreshNow(bool storeChanged)
{
    if (m_channels.isEmpty()) {
        return;
    }

    const qint64 now = QDateTime::currentSecsSinceEpoch();

    for (int i = 0; i < m_channels.size(); ++i) {
        const EpgEvent *current = m_store->eventAt(m_channels.at(i).uuid, now);
        const NowNext nowNext = {
            .now = current,
            .next = m_store->nextAfter(m_channels.at(i).uuid, current ? current->start : now),
        };

        // The events stayed where they were, so the pointers tell whether the programme changed.
        const bool changed =
            nowNext.now != m_nowNext.at(i).now || nowNext.next != m_nowNext.at(i).next;
        m_nowNext[i] = nowNext;

        if (changed && !storeChanged) {
            Q_EMIT dataChanged(index(i), index(i));
        }
    }

    // Every row's progress moves, so a single ranged notification is cheapest. Updating only
    // that role spares the delegates re-evaluating everything else.
    if (storeChanged) {
        Q_EMIT dataChanged(index(0), index(rowCount() - 1));
    } else {
        Q_EMIT dataChanged(index(0), index(rowCount() - 1), {NowProgressRole});
    }
}

int ChannelModel::indexOfUuid(const QString &uuid) const
{
    return m_rowOfUuid.value(uuid, -1);
}

int ChannelModel::indexOfNumber(int number) const
{
    for (int i = 0; i < m_channels.size(); ++i) {
        if (m_channels.at(i).number == number) {
            return i;
        }
    }

    return -1;
}

QVariantMap ChannelModel::get(int row) const
{
    QVariantMap result;
    if (row < 0 || row >= m_channels.size()) {
        return result;
    }

    const QHash<int, QByteArray> roles = roleNames();

    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        result.insert(QString::fromLatin1(it.value()), data(index(row), it.key()));
    }

    return result;
}

QString ChannelModel::uuidAt(int row) const
{
    return row >= 0 && row < m_channels.size() ? m_channels.at(row).uuid : QString();
}

QUrl ChannelModel::iconUrl(int row) const
{
    if (row < 0 || row >= m_channels.size()) {
        return {};
    }

    const QString icon = m_channels.at(row).icon;

    if (icon.isEmpty()) {
        return {};
    }

    // Local file paths (icon_public_url falls back to them) cannot be fetched remotely.
    if (icon.startsWith(QLatin1String("file://")) || icon.startsWith(QLatin1Char('/'))) {
        return {};
    }

    // Sources report absolute addresses; there is nothing to resolve others against.
    const QUrl url(icon);

    return url.isRelative() ? QUrl() : url;
}
