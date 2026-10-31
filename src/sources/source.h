/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SOURCE_H
#define SOURCE_H

#include "epgstore.h"
#include "headers.h"
#include "sourceconfig.h"

#include <QList>
#include <QObject>
#include <QPair>
#include <QUrl>
#include <QVector>

// One source of channels and guide data: a TVHeadend server, an M3U playlist,
// an Xtream Codes account or a Stalker portal. Channel and event IDs a source
// reports are its own; TvhBackend keeps the sources apart.
class Source : public QObject
{
    Q_OBJECT

public:
    struct Capabilities {
        bool record = false;
    };

    using Headers = ::Headers;

    // Where to fetch a stream from. TvhBackend relays it when it needs credentials or
    // headers, so the media player never sees them.
    struct StreamTarget {
        QUrl url;
        QString username;
        QString password;
        Headers headers;
        // The URL itself holds a secret (Xtream puts the password in the path).
        bool secretUrl = false;

        // Must go through the relay even when it is HLS, which the player could fetch itself.
        [[nodiscard]] bool needsRelay() const
        {
            return secretUrl || !username.isEmpty() || !headers.isEmpty()
                || !url.userInfo().isEmpty();
        }
    };

    // A new source of the type in 'config'; nullptr for an unknown type.
    static Source *create(const SourceConfig &config, QObject *parent = nullptr);

    explicit Source(SourceConfig config, QObject *parent = nullptr);

    [[nodiscard]] const SourceConfig &config() const
    {
        return m_config;
    }

    [[nodiscard]] QString id() const
    {
        return m_config.id;
    }

    [[nodiscard]] virtual Capabilities capabilities() const
    {
        return {};
    }
    // Changes when the cached channels and guide of this source become invalid.
    [[nodiscard]] virtual QString identity() const = 0;

    // Channels from the cache, before the first refresh() reports fresh ones.
    virtual void restoreChannels(const QVector<Channel> &channels);
    // Logs in, then reports the channels and the guide of the next 'guideDays' days.
    virtual void refresh(int guideDays) = 0;
    virtual void requestStream(const QString &channelId) = 0;
    // Checks the settings without changing anything; emits testFinished().
    virtual void test() = 0;
    // Only called with capabilities().record.
    virtual void record(const EpgEvent &event);
    virtual void cancelRecording(const EpgEvent &event);
    // Playing a channel failed for good; a source that can tell why emits streamFailed().
    virtual void explainStreamError(const QString &channelId);

Q_SIGNALS:
    void serverInfo(const QString &name, const QString &version);
    void channelsReady(const QVector<Channel> &channels);
    void guideReady(const QVector<EpgEvent> &events);
    void guideFailed(const QString &message);
    void streamReady(const QString &channelId, const Source::StreamTarget &target);
    void streamFailed(const QString &channelId, const QString &message);
    void recordingChanged(qint64 eventId, const QString &dvrUuid, const QString &dvrState);
    // Loading the channels failed; TvhBackend tries again later.
    void failed(const QString &message);
    void notification(const QString &message);
    void testFinished(bool ok, const QString &message);

protected:
    SourceConfig m_config;
};

#endif // SOURCE_H
