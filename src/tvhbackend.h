/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TVHBACKEND_H
#define TVHBACKEND_H

#include "epgdatabase.h"
#include "epgstore.h"
#include "xmltv.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <qqmlregistration.h>

class ChannelModel;
class StreamProxy;
class TvhApi;
class QQmlEngine;
class QJSEngine;

// Central coordinator: talks to the server (TVHeadend, or an M3U playlist with
// an XMLTV guide), maintains the EPG cache and exposes channels/events to QML.
class TvhBackend : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Tvh)
    QML_SINGLETON

    Q_PROPERTY(ChannelModel *channels READ channels CONSTANT)
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString serverName READ serverName NOTIFY serverInfoChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY serverInfoChanged)
    // Recordings need TVHeadend; M3U sources only play live.
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY serverInfoChanged)
    Q_PROPERTY(QDateTime lastUpdate READ lastUpdate NOTIFY epgChanged)
    Q_PROPERTY(bool rewindAvailable READ rewindAvailable NOTIFY rewindChanged)
    Q_PROPERTY(int epgRevision READ epgRevision NOTIFY epgChanged)
    Q_PROPERTY(double epgEnd READ epgEnd NOTIFY epgChanged)
    Q_PROPERTY(double now READ now NOTIFY tick)

public:
    enum Status : uint8_t {
        Unconfigured,
        Connecting,
        Online,
        Offline,
    };
    Q_ENUM(Status)

    static TvhBackend *instance();
    static TvhBackend *create(QQmlEngine *, QJSEngine *);

    [[nodiscard]] ChannelModel *channels() const
    {
        return m_channels;
    }

    [[nodiscard]] Status status() const
    {
        return m_status;
    }

    [[nodiscard]] QString statusText() const
    {
        return m_statusText;
    }

    [[nodiscard]] bool busy() const
    {
        return m_busy;
    }

    [[nodiscard]] QString serverName() const
    {
        return m_serverName;
    }

    [[nodiscard]] QString serverVersion() const
    {
        return m_serverVersion;
    }

    [[nodiscard]] bool canRecord() const;

    [[nodiscard]] QDateTime lastUpdate() const
    {
        return m_lastUpdate;
    }

    [[nodiscard]] int epgRevision() const
    {
        return m_epgRevision;
    }

    [[nodiscard]] double epgEnd() const
    {
        return static_cast<double>(m_store.lastStop()) * 1000.0;
    }

    [[nodiscard]] static double now()
    {
        return static_cast<double>(QDateTime::currentMSecsSinceEpoch());
    }

    Q_INVOKABLE void refresh();

    // values: same keys as Settings.save(); password may be omitted to use the stored one.
    Q_INVOKABLE void testConnection(const QVariantMap &values);
    Q_INVOKABLE void clearCache();

    Q_INVOKABLE [[nodiscard]] QVariantList
    eventsForChannel(const QString &uuid, double fromMs, double toMs) const;

    Q_INVOKABLE [[nodiscard]] QVariantMap eventAt(const QString &uuid, double timeMs) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap nextEvent(const QString &uuid, double afterMs) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap eventById(double id) const;

    Q_INVOKABLE void requestStream(const QString &uuid);

    // Rewinding the playing channel. See StreamProxy for what is kept on disk.
    [[nodiscard]] bool rewindAvailable() const;
    Q_INVOKABLE [[nodiscard]] double rewindWindowMs() const;
    // Keeps the stream from now on while paused with rewinding off. False when it can't be
    // kept (HLS or not MPEG-TS): the player then pauses plainly.
    Q_INVOKABLE bool holdStream();
    // The playing channel 'behindMs' behind live; 0 is live.
    Q_INVOKABLE [[nodiscard]] QUrl timeshiftUrl(double behindMs) const;
    // Ends the playing channel's stream and deletes what was kept of it.
    Q_INVOKABLE void stopStream();

    // The server's address of a channel's stream, without credentials. Empty if unknown.
    [[nodiscard]] QUrl streamUrl(const QString &uuid) const;

    Q_INVOKABLE void record(double eventId);
    Q_INVOKABLE void cancelRecording(double eventId);

Q_SIGNALS:
    void statusChanged();
    void busyChanged();
    void serverInfoChanged();
    void epgChanged();
    void tick();
    void streamReady(const QString &uuid, const QUrl &url);
    void streamFailed(const QString &uuid, const QString &message);
    void connectionTestFinished(bool ok, const QString &message);
    void notification(const QString &message);
    void rewindChanged();

private:
    explicit TvhBackend(QObject *parent = nullptr);

    void applySettings(bool serverChanged);
    void loadCache();
    void fetchChannels(int generation);
    void fetchPlaylist(int generation);
    void fetchGuide(int generation, const QUrl &url, const Xmltv::ChannelMap &map);
    void fetchEpgPage(int generation, int offset);
    void finishEpg(int generation);
    void setStatus(Status status, const QString &text);
    void setBusy(bool busy);
    void setRecordingState(qint64 eventId, const QString &dvrUuid, const QString &dvrState);

    TvhApi *m_api;
    ChannelModel *m_channels;
    StreamProxy *m_streamProxy;
    QUrl m_playingRelay;
    bool m_rewindStopped = false;
    EpgStore m_store;
    EpgDatabase m_database;
    QVector<EpgEvent> m_incoming;

    Status m_status = Unconfigured;
    QString m_statusText;
    bool m_busy = false;
    int m_generation = 0;
    QString m_serverName;
    QString m_serverVersion;
    QDateTime m_lastUpdate;
    int m_epgRevision = 0;

    QTimer m_tickTimer;
    QTimer m_epgTimer;
    QTimer m_retryTimer;
};

#endif // TVHBACKEND_H
