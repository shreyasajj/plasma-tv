// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tvhbackend.h"

#include "appsettings.h"
#include "channelmodel.h"
#include "sources/source.h"
#include "streamproxy.h"

#include <QQmlEngine>

namespace
{
constexpr int s_tickMs = 15 * 1000;
constexpr int s_epgRefreshMs = 30 * 60 * 1000;
constexpr int s_retryMs = 60 * 1000;

ServerConfig configFromValues(const QVariantMap &values, const ServerConfig &fallback)
{
    ServerConfig config = fallback;
    config.backend = ServerConfig::backendFromString(
        values.value(QStringLiteral("backend"), ServerConfig::backendName(config.backend))
            .toString());
    config.playlistUrl =
        values.value(QStringLiteral("playlistUrl"), config.playlistUrl).toString().trimmed();
    config.xmltvUrl =
        values.value(QStringLiteral("xmltvUrl"), config.xmltvUrl).toString().trimmed();
    config.host = values.value(QStringLiteral("host"), config.host).toString().trimmed();
    config.port = values.value(QStringLiteral("port"), config.port).toInt();
    config.https = values.value(QStringLiteral("useHttps"), config.https).toBool();
    config.username =
        values.value(QStringLiteral("username"), config.username).toString().trimmed();

    if (values.contains(QStringLiteral("password"))) {
        config.password = values.value(QStringLiteral("password")).toString();
    }

    return config;
}
}

TvhBackend *TvhBackend::instance()
{
    static TvhBackend *s_instance = new TvhBackend;
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
    connect(&m_tickTimer, &QTimer::timeout, this, [this]() {
        m_channels->refreshNow(false);
        Q_EMIT tick();
    });
    m_tickTimer.start();

    m_epgTimer.setInterval(s_epgRefreshMs);
    connect(&m_epgTimer, &QTimer::timeout, this, &TvhBackend::refresh);

    m_retryTimer.setInterval(s_retryMs);
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &TvhBackend::refresh);

    AppSettings *settings = AppSettings::instance();
    connect(settings, &AppSettings::saved, this, &TvhBackend::applySettings);
    connect(settings, &AppSettings::credentialsReadyChanged, this, [this]() {
        applySettings(true);
    });

    createSource();
    m_channels->setBaseUrl(settings->server().baseUrl());
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
    return m_source && m_source->capabilities().record;
}

void TvhBackend::createSource()
{
    delete m_source;
    m_source = nullptr;

    const ServerConfig server = AppSettings::instance()->server();
    if (!server.isValid()) {
        return;
    }

    m_source = Source::create(SourceConfig::fromServer(server), this);
    connect(m_source,
            &Source::serverInfo,
            this,
            [this](const QString &name, const QString &version) {
                m_serverName = name;
                m_serverVersion = version;
                Q_EMIT serverInfoChanged();
            });
    connect(m_source, &Source::channelsReady, this, [this](const QVector<Channel> &channels) {
        m_database.setServerIdentity(m_source->identity());
        m_database.saveChannels(channels);
        m_channels->setChannels(channels);
        setStatus(Online, tr("Connected"));
        m_epgTimer.start();
    });
    connect(m_source, &Source::guideReady, this, &TvhBackend::finishEpg);
    connect(m_source, &Source::guideFailed, this, [this](const QString &message) {
        setBusy(false);
        Q_EMIT notification(message);
    });
    connect(m_source, &Source::failed, this, [this](const QString &message) {
        setBusy(false);
        setStatus(Offline, message);
        m_retryTimer.start();
    });
    connect(m_source, &Source::streamReady, this, &TvhBackend::playStream);
    connect(m_source, &Source::streamFailed, this, &TvhBackend::streamFailed);
    connect(m_source, &Source::recordingChanged, this, &TvhBackend::setRecordingState);
    connect(m_source, &Source::notification, this, &TvhBackend::notification);
}

void TvhBackend::applySettings(bool serverChanged)
{
    const ServerConfig server = AppSettings::instance()->server();
    if (serverChanged) {
        createSource();
        m_channels->setBaseUrl(server.baseUrl());
        m_serverName.clear();
        m_serverVersion.clear();
        Q_EMIT serverInfoChanged();

        if (m_database.serverIdentity() != server.identity()) {
            clearCache();
        }
    }

    refresh();
}

void TvhBackend::loadCache()
{
    if (!m_source || m_database.serverIdentity() != m_source->identity()) {
        return;
    }

    // The channels are few and wanted right away; the guide follows when it is loaded.
    const QVector<Channel> channels = m_database.loadChannels();
    m_source->restoreChannels(channels);
    m_channels->setChannels(channels);
    const qint64 last = m_database.lastUpdate();
    m_lastUpdate = last > 0 ? QDateTime::fromSecsSinceEpoch(last) : QDateTime();
    const int revision = m_epgRevision;

    m_database.loadEvents(QDateTime::currentSecsSinceEpoch(),
                          this,
                          [this, revision](const QVector<EpgEvent> &events) {
                              // Fresh data from the server (or a cleared cache) wins.
                              if (revision != m_epgRevision) {
                                  return;
                              }

                              m_store.setEvents(events);
                              m_channels->refreshNow();
                              ++m_epgRevision;
                              Q_EMIT epgChanged();
                          });
}

void TvhBackend::clearCache()
{
    m_database.clear();
    m_store.clear();
    m_channels->setChannels({});
    m_lastUpdate = {};
    ++m_epgRevision;
    Q_EMIT epgChanged();
}

void TvhBackend::refresh()
{
    AppSettings *settings = AppSettings::instance();
    m_retryTimer.stop();

    if (!settings->configured()) {
        setStatus(Unconfigured, tr("Not configured"));
        return;
    }

    if (!settings->credentialsReady()) {
        return;
    }

    if (!m_source) {
        createSource();
    }

    setBusy(true);

    if (m_status != Online) {
        const QString host = settings->server().baseUrl().host();
        setStatus(Connecting,
                  host.isEmpty() ? tr("Loading the playlist…") : tr("Connecting to %1…").arg(host));
    }

    m_source->refresh(settings->guideDays());
}

void TvhBackend::finishEpg(QVector<EpgEvent> events)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_database.saveEvents(events, now);
    m_store.setEvents(std::move(events));
    m_lastUpdate = QDateTime::fromSecsSinceEpoch(now);
    m_channels->refreshNow();
    ++m_epgRevision;
    setBusy(false);
    Q_EMIT epgChanged();
}

void TvhBackend::testConnection(const QVariantMap &values)
{
    const ServerConfig config = configFromValues(values, AppSettings::instance()->server());
    Source *source = Source::create(SourceConfig::fromServer(config), this);
    connect(source, &Source::testFinished, this, [this, source](bool ok, const QString &message) {
        source->deleteLater();
        Q_EMIT connectionTestFinished(ok, message);
    });
    source->test();
}

QVariantList TvhBackend::eventsForChannel(const QString &uuid, double fromMs, double toMs) const
{
    QVariantList result;
    const auto events = m_store.range(uuid, qint64(fromMs / 1000.0), qint64(toMs / 1000.0));
    result.reserve(events.size());

    for (const EpgEvent *event : events) {
        result.append(event->toVariant());
    }

    return result;
}

QVariantMap TvhBackend::eventAt(const QString &uuid, double timeMs) const
{
    const EpgEvent *event = m_store.eventAt(uuid, qint64(timeMs / 1000.0));
    return event ? event->toVariant() : QVariantMap();
}

QVariantMap TvhBackend::nextEvent(const QString &uuid, double afterMs) const
{
    const EpgEvent *event = m_store.nextAfter(uuid, qint64(afterMs / 1000.0));
    return event ? event->toVariant() : QVariantMap();
}

QVariantMap TvhBackend::eventById(double id) const
{
    const EpgEvent *event = m_store.byId(qint64(id));
    return event ? event->toVariant() : QVariantMap();
}

void TvhBackend::requestStream(const QString &uuid)
{
    if (!m_source) {
        Q_EMIT streamFailed(uuid, tr("Not configured"));
        return;
    }

    m_source->requestStream(uuid);
}

void TvhBackend::playStream(const QString &uuid, const Source::StreamTarget &target)
{
    if (!target.needsRelay()) {
        Q_EMIT streamReady(uuid, target.url);
        return;
    }

    // The player can't answer authentication challenges and would log credentials in the
    // URL, so these streams go through the local relay.
    ServerConfig credentials;
    credentials.username = target.username;
    credentials.password = target.password;
    QUrl upstream = target.url;

    if (!upstream.userName().isEmpty()) {
        credentials.username = upstream.userName();
        credentials.password = upstream.password();
        upstream.setUserInfo({});
    }

    const QUrl url = m_streamProxy->relay(upstream, credentials);
    if (url.isEmpty()) {
        Q_EMIT streamFailed(uuid, tr("The local stream relay could not be started"));
        return;
    }

    Q_EMIT streamReady(uuid, url);
}

void TvhBackend::record(double eventId)
{
    const EpgEvent *event = m_store.byId(qint64(eventId));
    if (!event || !canRecord()) {
        return;
    }

    m_source->record(*event);
}

void TvhBackend::cancelRecording(double eventId)
{
    const EpgEvent *event = m_store.byId(qint64(eventId));
    if (!event || event->dvrUuid.isEmpty() || !canRecord()) {
        return;
    }

    m_source->cancelRecording(*event);
}

void TvhBackend::setRecordingState(qint64 eventId, const QString &dvrUuid, const QString &dvrState)
{
    EpgEvent *event = m_store.byId(eventId);
    if (!event) {
        return;
    }

    event->dvrUuid = dvrUuid;
    event->dvrState = dvrState;
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
