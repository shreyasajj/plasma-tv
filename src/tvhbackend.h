/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TVHBACKEND_H
#define TVHBACKEND_H

#include "epgdatabase.h"
#include "epgstore.h"
#include "sources/source.h"

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <qqmlregistration.h>

class ChannelModel;
class StreamProxy;
class QQmlEngine;
class QJSEngine;

// Central coordinator: gets channels, the guide and streams from the enabled
// sources (see Source and SourceSettings), merges them into one channel list
// and guide, maintains the EPG cache and exposes channels/events to QML.
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
    // Recordings need a source that records (TVHeadend); the others only play live.
    // canRecord: at least one source can; canRecordChannel(): the channel's source can.
    Q_PROPERTY(bool canRecord READ canRecord NOTIFY serverInfoChanged)
    // One entry per enabled source: id, name, type, status (Status), text.
    Q_PROPERTY(QVariantList sourceStatus READ sourceStatus NOTIFY sourceStatusChanged)
    Q_PROPERTY(QDateTime lastUpdate READ lastUpdate NOTIFY epgChanged)
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
    Q_INVOKABLE [[nodiscard]] bool canRecordChannel(const QString &uuid) const;
    [[nodiscard]] QVariantList sourceStatus() const;

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

    // values: same keys as Sources.save(); secrets may be omitted to use the stored ones.
    Q_INVOKABLE void testConnection(const QVariantMap &values);
    Q_INVOKABLE void clearCache();

    Q_INVOKABLE [[nodiscard]] QVariantList
    eventsForChannel(const QString &uuid, double fromMs, double toMs) const;

    Q_INVOKABLE [[nodiscard]] QVariantMap eventAt(const QString &uuid, double timeMs) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap nextEvent(const QString &uuid, double afterMs) const;
    Q_INVOKABLE [[nodiscard]] QVariantMap eventById(double id) const;

    Q_INVOKABLE void requestStream(const QString &uuid);
    Q_INVOKABLE void record(double eventId);
    Q_INVOKABLE void cancelRecording(double eventId);

Q_SIGNALS:
    void statusChanged();
    void sourceStatusChanged();
    void busyChanged();
    void serverInfoChanged();
    void epgChanged();
    void tick();
    void streamReady(const QString &uuid, const QUrl &url);
    void streamFailed(const QString &uuid, const QString &message);
    void connectionTestFinished(bool ok, const QString &message);
    void notification(const QString &message);

private:
    struct SourceState {
        Source *source = nullptr;
        QVector<Channel> channels; // with prefixed IDs
        QVector<EpgEvent> events; // with prefixed IDs
        Status status = Connecting;
        QString statusText;
        bool busy = false;
        QString serverName;
        QString serverVersion;
    };

    explicit TvhBackend(QObject *parent = nullptr);

    void createSources();
    void createSource(const SourceConfig &config, int position);
    // Deletes the source with this ID and forgets its state.
    void removeSource(const QString &id);
    void sourceSaved(const QString &id);
    void sourceRemoved(const QString &id);
    void sourcesReordered();
    SourceState *state(const QString &id);
    [[nodiscard]] const SourceState *stateOfChannel(const QString &uuid) const;
    static void refreshSource(SourceState &state);
    void refreshOffline();
    void loadCache();
    void mergeChannels();
    void mergeEvents();
    void updateStatus();
    void playStream(const QString &uuid, const Source::StreamTarget &target);
    void setStatus(Status status, const QString &text);
    void setBusy(bool busy);
    void setRecordingState(qint64 eventId, const QString &dvrUuid, const QString &dvrState);

    QList<SourceState> m_sources; // enabled sources, in the order of the settings
    ChannelModel *m_channels;
    StreamProxy *m_streamProxy;
    EpgStore m_store;
    EpgDatabase m_database;

    Status m_status = Unconfigured;
    QString m_statusText;
    bool m_busy = false;
    QString m_serverName;
    QString m_serverVersion;
    QDateTime m_lastUpdate;
    int m_epgRevision = 0;

    QTimer m_tickTimer;
    QTimer m_epgTimer;
    QTimer m_retryTimer;
};

#endif // TVHBACKEND_H
