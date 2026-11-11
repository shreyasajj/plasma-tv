// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tvhbackend.h"

#include "appsettings.h"
#include "channelmodel.h"
#include "sources/channelid.h"
#include "sources/sourcesettings.h"
#include "streamproxy.h"

#include <QQmlEngine>

#include <algorithm>

namespace
{
constexpr int s_tickMs = 15 * 1000;
constexpr int s_epgRefreshMs = 30 * 60 * 1000;
constexpr int s_retryMs = 60 * 1000;
}

TvhBackend *TvhBackend::instance()
{
    static auto s_instance = new TvhBackend;

    return s_instance;
}

TvhBackend *TvhBackend::create(QQmlEngine *, QJSEngine *)
{
    auto *self = instance();
    QQmlEngine::setObjectOwnership(self, QQmlEngine::CppOwnership);
    return self;
}

TvhBackend::TvhBackend(QObject *parent)
    : QObject(parent)
    , m_channels(new ChannelModel(&m_store, this))
    , m_streamProxy(new StreamProxy(this))
{
    m_database.open();

    m_tickTimer.setInterval(s_tickMs);
    connect(&m_tickTimer, &QTimer::timeout, this, [this] {
        m_channels->refreshNow(false);
        Q_EMIT tick();
    });

    m_tickTimer.start();

    m_epgTimer.setInterval(s_epgRefreshMs);
    connect(&m_epgTimer, &QTimer::timeout, this, &TvhBackend::refresh);

    m_retryTimer.setInterval(s_retryMs);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &TvhBackend::refreshOffline);

    // Guide days or the streaming profile changed.
    connect(AppSettings::instance(), &AppSettings::saved, this, &TvhBackend::refresh);

    const SourceSettings *settings = SourceSettings::instance();
    connect(settings, &SourceSettings::saved, this, &TvhBackend::sourceSaved);
    connect(settings, &SourceSettings::removed, this, &TvhBackend::sourceRemoved);
    connect(settings, &SourceSettings::reordered, this, &TvhBackend::sourcesReordered);
    connect(settings, &SourceSettings::credentialsReadyChanged, this, [this]() {
        // The new sources know nothing yet: show the cached channels until theirs arrive.
        createSources();
        loadCache();
        refresh();
    });

    // Forget the cache of sources that were removed meanwhile.
    QStringList ids;

    for (const SourceConfig &config : settings->configs()) {
        ids.append(config.id);
    }

    for (const QString &id : m_database.sources()) {
        if (!ids.contains(id)) {
            m_database.clearSource(id);
        }
    }

    createSources();
    loadCache();

    if (!settings->configured()) {
        setStatus(Unconfigured, tr("Not configured"));
    } else if (settings->credentialsReady()) {
        refresh();
    } else {
        setStatus(Connecting, tr("Unlocking credentials…"));
    }
}

bool TvhBackend::canRecord() const
{
    return std::ranges::any_of(m_sources, [](const SourceState &state) {
        return state.source->capabilities().record;
    });
}

bool TvhBackend::canRecordChannel(const QString &uuid) const
{
    const SourceState *state = stateOfChannel(uuid);

    return state && state->source->capabilities().record;
}

QVariantList TvhBackend::sourceStatus() const
{
    QVariantList list;

    for (const SourceState &state : m_sources) {
        list.append(QVariantMap{{QStringLiteral("id"), state.source->id()},
                                {QStringLiteral("name"), state.source->config().name},
                                {QStringLiteral("type"), state.source->config().type},
                                {QStringLiteral("status"), static_cast<int>(state.status)},
                                {QStringLiteral("text"), state.statusText}});
    }

    return list;
}

void TvhBackend::createSources()
{
    for (const SourceState &state : std::as_const(m_sources)) {
        delete state.source;
    }

    m_sources.clear();

    for (const SourceConfig &config : SourceSettings::instance()->configs()) {
        createSource(config, static_cast<int>(m_sources.size()));
    }
}

void TvhBackend::createSource(const SourceConfig &config, int position)
{
    if (!config.enabled) {
        return;
    }

    Source *source = Source::create(config, this);

    if (!source) {
        qWarning() << "Unknown source type" << config.type;

        return;
    }

    SourceState state;
    state.source = source;
    m_sources.insert(position, state);

    const QString id = config.id;
    const int index = config.index;

    connect(source,
            &Source::serverInfo,
            this,
            [this, id](const QString &name, const QString &version) {
                if (SourceState *state = this->state(id)) {
                    state->serverName = name;
                    state->serverVersion = version;
                }

                m_serverName = m_sources.isEmpty() ? QString() : m_sources.first().serverName;
                m_serverVersion = m_sources.isEmpty() ? QString() : m_sources.first().serverVersion;
                Q_EMIT serverInfoChanged();
            });
    connect(source, &Source::channelsReady, this, [this, id](const QVector<Channel> &channels) {
        SourceState *state = this->state(id);

        if (!state) {
            return;
        }

        state->channels = ChannelId::prefixed(id, channels);
        state->status = Online;
        state->statusText = tr("Connected");
        m_database.setSourceIdentity(id, state->source->identity());
        m_database.saveChannels(id, state->channels);
        mergeChannels();
        updateStatus();
        m_epgTimer.start();
    });
    connect(source, &Source::guideReady, this, [this, id, index](const QVector<EpgEvent> &events) {
        SourceState *state = this->state(id);

        if (!state) {
            return;
        }

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        state->events = ChannelId::prefixed(id, index, events);
        state->busy = false;
        m_database.saveEvents(id, state->events, now);
        m_lastUpdate = QDateTime::fromSecsSinceEpoch(now);
        mergeEvents();
        updateStatus();
    });
    connect(source, &Source::guideFailed, this, [this, id](const QString &message) {
        if (SourceState *state = this->state(id)) {
            state->busy = false;
            Q_EMIT notification(
                m_sources.size() > 1
                    ? QStringLiteral("%1: %2").arg(state->source->config().name, message)
                    : message);
        }

        updateStatus();
    });
    connect(source, &Source::failed, this, [this, id](const QString &message) {
        if (SourceState *state = this->state(id)) {
            state->busy = false;
            state->status = Offline;
            state->statusText = message;
        }

        updateStatus();
        m_retryTimer.start();
    });
    connect(source,
            &Source::streamReady,
            this,
            [this, id](const QString &channelId, const Source::StreamTarget &target) {
                playStream(ChannelId::make(id, channelId), target);
            });
    connect(source,
            &Source::streamFailed,
            this,
            [this, id](const QString &channelId, const QString &message) {
                Q_EMIT streamFailed(ChannelId::make(id, channelId), message);
            });
    connect(source,
            &Source::recordingChanged,
            this,
            [this, index](qint64 eventId, const QString &dvrUuid, const QString &dvrState) {
                setRecordingState(ChannelId::makeEvent(index, eventId), dvrUuid, dvrState);
            });
    connect(source, &Source::notification, this, &TvhBackend::notification);
}

TvhBackend::SourceState *TvhBackend::state(const QString &id)
{
    for (SourceState &state : m_sources) {
        if (state.source->id() == id) {
            return &state;
        }
    }

    return nullptr;
}

const TvhBackend::SourceState *TvhBackend::stateOfChannel(const QString &uuid) const
{
    const QString id = ChannelId::sourceOf(uuid);

    for (const SourceState &state : m_sources) {
        if (state.source->id() == id) {
            return &state;
        }
    }

    return nullptr;
}

void TvhBackend::removeSource(const QString &id)
{
    const auto found = std::ranges::find_if(m_sources, [&id](const SourceState &state) {
        return state.source->id() == id;
    });

    if (found == m_sources.end()) {
        return;
    }

    delete found->source;
    m_sources.erase(found);
}

void TvhBackend::sourceSaved(const QString &id)
{
    // Replaced by a new source with the new settings, at its place in the list.
    const SourceConfig config = SourceSettings::instance()->config(id);
    int position = 0;

    for (const SourceConfig &other : SourceSettings::instance()->configs()) {
        if (other.id == id) {
            break;
        }

        if (state(other.id)) {
            ++position;
        }
    }

    removeSource(id);

    createSource(config, position);
    SourceState *created = state(id);

    if (!created || m_database.sourceIdentity(id) != created->source->identity()) {
        m_database.clearSource(id);
        mergeChannels();
        mergeEvents();
    } else {
        // Same server: keep showing its channels until the new ones arrive.
        created->channels = {};
        loadCache();
    }

    if (created && SourceSettings::instance()->credentialsReady()) {
        refreshSource(*created);
    }

    updateStatus();
    Q_EMIT serverInfoChanged();
}

void TvhBackend::sourceRemoved(const QString &id)
{
    removeSource(id);
    m_database.clearSource(id);
    mergeChannels();
    mergeEvents();
    updateStatus();
    Q_EMIT serverInfoChanged();
}

void TvhBackend::sourcesReordered()
{
    QList<SourceState> ordered;

    for (const SourceConfig &config : SourceSettings::instance()->configs()) {
        if (const SourceState *found = state(config.id)) {
            ordered.append(*found);
        }
    }

    m_sources = ordered;
    mergeChannels();
    updateStatus();
}

void TvhBackend::loadCache()
{
    // The channels are few and wanted right away; the guide follows when it is loaded.
    QStringList cached;
    const QVector<Channel> channels = m_database.loadChannels();

    for (SourceState &state : m_sources) {
        const QString id = state.source->id();

        if (m_database.sourceIdentity(id) != state.source->identity()
            || !state.channels.isEmpty()) {
            continue;
        }

        const QString prefix = id + QLatin1Char(':');
        QVector<Channel> own;

        for (const Channel &channel : channels) {
            if (channel.uuid.startsWith(prefix)) {
                state.channels.append(channel);
                Channel unprefixed = channel;
                unprefixed.uuid = ChannelId::ownOf(channel.uuid);
                own.append(unprefixed);
            }
        }

        state.source->restoreChannels(own);
        cached.append(id);
    }

    if (cached.isEmpty()) {
        return;
    }

    mergeChannels();
    const qint64 last = m_database.lastUpdate();
    m_lastUpdate = last > 0 ? QDateTime::fromSecsSinceEpoch(last) : QDateTime();
    const int revision = m_epgRevision;

    m_database.loadEvents(QDateTime::currentSecsSinceEpoch(),
                          this,
                          [this, revision, cached](const QVector<EpgEvent> &events) {
                              // Fresh data from the server (or a cleared cache) wins.
                              if (revision != m_epgRevision) {
                                  return;
                              }

                              for (SourceState &state : m_sources) {
                                  const QString id = state.source->id();

                                  if (!cached.contains(id) || !state.events.isEmpty()) {
                                      continue;
                                  }

                                  const QString prefix = id + QLatin1Char(':');

                                  for (const EpgEvent &event : events) {
                                      if (event.channel.startsWith(prefix)) {
                                          state.events.append(event);
                                      }
                                  }
                              }

                              mergeEvents();
                          });
}

void TvhBackend::mergeChannels()
{
    QList<QVector<Channel>> lists;

    for (const SourceState &state : std::as_const(m_sources)) {
        lists.append(state.channels);
    }

    m_channels->setChannels(lists.isEmpty() ? QVector<Channel>() : ChannelId::merge(lists));
}

void TvhBackend::mergeEvents()
{
    QVector<EpgEvent> events;

    for (const SourceState &state : std::as_const(m_sources)) {
        events.append(state.events);
    }

    m_store.setEvents(std::move(events));
    m_channels->refreshNow();
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::updateStatus()
{
    const bool busy = std::ranges::any_of(m_sources, [](const SourceState &state) {
        return state.busy;
    });
    setBusy(busy);
    // A source's state can change while the overall one stays the same.
    Q_EMIT sourceStatusChanged();

    if (m_sources.isEmpty()) {
        setStatus(Unconfigured, tr("Not configured"));

        return;
    }

    const auto count = [this](Status status) {
        return std::count_if(m_sources.cbegin(),
                             m_sources.cend(),
                             [status](const SourceState &state) {
                                 return state.status == status;
                             });
    };
    const bool several = m_sources.size() > 1;
    const auto named = [several](const SourceState &state) {
        return several ? QStringLiteral("%1: %2").arg(state.source->config().name, state.statusText)
                       : state.statusText;
    };

    const auto offline = std::ranges::find_if(m_sources, [](const SourceState &state) {
        return state.status == Offline;
    });

    if (count(Online) > 0) {
        // One source failing while others work is still worth a word.
        setStatus(Online, offline != m_sources.cend() ? named(*offline) : tr("Connected"));
    } else if (count(Connecting) > 0) {
        const auto connecting = std::ranges::find_if(m_sources, [](const SourceState &state) {
            return state.status == Connecting;
        });
        setStatus(Connecting, connecting->statusText);
    } else {
        setStatus(Offline, named(*offline));
    }
}

void TvhBackend::clearCache()
{
    m_database.clear();

    for (SourceState &state : m_sources) {
        state.channels.clear();
        state.events.clear();
    }

    m_store.clear();
    m_channels->setChannels({});
    m_lastUpdate = {};
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::refresh()
{
    m_retryTimer.stop();

    if (!SourceSettings::instance()->configured() || m_sources.isEmpty()) {
        setStatus(Unconfigured, tr("Not configured"));
        return;
    }

    if (!SourceSettings::instance()->credentialsReady()) {
        return;
    }

    for (SourceState &state : m_sources) {
        refreshSource(state);
    }

    updateStatus();
}

void TvhBackend::refreshSource(SourceState &state)
{
    state.busy = true;

    if (state.status != Online) {
        state.status = Connecting;
        state.statusText = tr("Connecting to %1…").arg(state.source->config().name);
    }

    state.source->refresh(AppSettings::instance()->guideDays());
}

void TvhBackend::refreshOffline()
{
    for (SourceState &state : m_sources) {
        if (state.status == Offline) {
            refreshSource(state);
        }
    }

    updateStatus();
}

void TvhBackend::testConnection(const QVariantMap &values)
{
    Source *source = Source::create(SourceSettings::instance()->draft(values), this);

    if (!source) {
        Q_EMIT connectionTestFinished(false, tr("Unknown kind of source"));

        return;
    }

    connect(source, &Source::testFinished, this, [this, source](bool ok, const QString &message) {
        source->deleteLater();
        Q_EMIT connectionTestFinished(ok, message);
    });
    source->test();
}

QVariantList TvhBackend::eventsForChannel(const QString &uuid, double fromMs, double toMs) const
{
    QVariantList result;
    const auto events = m_store.range(uuid,
                                      static_cast<qint64>(fromMs / 1000.0),
                                      static_cast<qint64>(toMs / 1000.0));
    result.reserve(events.size());

    for (const EpgEvent *event : events) {
        result.append(event->toVariant());
    }

    return result;
}

QVariantMap TvhBackend::eventAt(const QString &uuid, double timeMs) const
{
    const EpgEvent *event = m_store.eventAt(uuid, static_cast<qint64>(timeMs / 1000.0));

    return event ? event->toVariant() : QVariantMap();
}

QVariantMap TvhBackend::nextEvent(const QString &uuid, double afterMs) const
{
    const EpgEvent *event = m_store.nextAfter(uuid, static_cast<qint64>(afterMs / 1000.0));

    return event ? event->toVariant() : QVariantMap();
}

QVariantMap TvhBackend::eventById(double id) const
{
    const EpgEvent *event = m_store.byId(static_cast<qint64>(id));

    return event ? event->toVariant() : QVariantMap();
}

void TvhBackend::requestStream(const QString &uuid)
{
    const SourceState *state = stateOfChannel(uuid);

    if (!state) {
        Q_EMIT streamFailed(uuid, tr("The channel's source is not available"));
        return;
    }

    state->source->requestStream(ChannelId::ownOf(uuid));
}

void TvhBackend::explainStreamError(const QString &uuid)
{
    if (const SourceState *state = stateOfChannel(uuid)) {
        state->source->explainStreamError(ChannelId::ownOf(uuid));
    }
}

void TvhBackend::playStream(const QString &uuid, const Source::StreamTarget &target)
{
    // HLS is fetched segment by segment by the player, which already buffers it. One that needs
    // a login or headers still goes through the relay, which sends them for the playlist.
    if (StreamProxy::isHls(target.url) && !target.needsRelay()) {
        Q_EMIT streamReady(uuid, target.url);

        return;
    }

    // Every other stream goes through the relay: it buffers, reconnects without the player
    // noticing and keeps credentials out of the media URL.
    const QUrl url = m_streamProxy->relay({.url = target.url,
                                           .username = target.username,
                                           .password = target.password,
                                           .bufferMode = AppSettings::instance()->playbackBuffer(),
                                           .headers = target.headers,
                                           .secretUrl = target.secretUrl});

    if (url.isEmpty()) {
        Q_EMIT streamFailed(uuid, tr("The local stream relay could not be started"));

        return;
    }

    Q_EMIT streamReady(uuid, url);
}

void TvhBackend::record(double eventId)
{
    const EpgEvent *event = m_store.byId(static_cast<qint64>(eventId));

    if (!event || !canRecordChannel(event->channel)) {
        return;
    }

    // The source knows its own IDs.
    EpgEvent own = *event;
    own.id = ChannelId::ownEventOf(event->id);
    own.channel = ChannelId::ownOf(event->channel);
    stateOfChannel(event->channel)->source->record(own);
}

void TvhBackend::cancelRecording(double eventId)
{
    const EpgEvent *event = m_store.byId(static_cast<qint64>(eventId));

    if (!event || event->dvrUuid.isEmpty() || !canRecordChannel(event->channel)) {
        return;
    }

    EpgEvent own = *event;
    own.id = ChannelId::ownEventOf(event->id);
    own.channel = ChannelId::ownOf(event->channel);
    stateOfChannel(event->channel)->source->cancelRecording(own);
}

void TvhBackend::setRecordingState(qint64 eventId, const QString &dvrUuid, const QString &dvrState)
{
    EpgEvent *event = m_store.byId(eventId);
    if (!event) {
        return;
    }

    event->dvrUuid = dvrUuid;
    event->dvrState = dvrState;

    // Also in the source's copy, which the guide is merged from again later.
    if (SourceState *state = this->state(ChannelId::sourceOf(event->channel))) {
        for (EpgEvent &own : state->events) {
            if (own.id == eventId) {
                own.dvrUuid = dvrUuid;
                own.dvrState = dvrState;
                break;
            }
        }
    }

    m_database.updateRecording(eventId, dvrUuid, dvrState);
    m_channels->refreshNow();
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::setStatus(Status status, const QString &text)
{
    if (status == m_status && text == m_statusText) {
        return;
    }

    m_status = status;
    m_statusText = text;
    Q_EMIT statusChanged();
}

void TvhBackend::setBusy(bool busy)
{
    if (busy == m_busy) {
        return;
    }

    m_busy = busy;
    Q_EMIT busyChanged();
}
