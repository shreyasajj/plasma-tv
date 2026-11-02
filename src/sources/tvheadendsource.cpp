// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tvheadendsource.h"

#include "appsettings.h"
#include "tvhapi.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>

namespace
{
constexpr int s_epgPageSize = 2000;
constexpr int s_maxEpgEvents = 200000;

QJsonArray entriesOf(const QJsonDocument &document)
{
    return document.object().value(QLatin1String("entries")).toArray();
}

QString firstUuid(const QJsonDocument &document)
{
    const QJsonValue uuid = document.object().value(QLatin1String("uuid"));
    if (uuid.isArray()) {
        return uuid.toArray().first().toString();
    }

    return uuid.toString();
}
}

TvheadendSource::TvheadendSource(const SourceConfig &config, QObject *parent)
    : Source(config, parent)
    , m_api(new TvhApi(this))
    , m_server(config.serverConfig())
{
    m_api->setServer(m_server);
}

QString TvheadendSource::identity() const
{
    return m_server.identity();
}

void TvheadendSource::refresh(int guideDays)
{
    // Answers of an older refresh are ignored by their generation. Aborting everything instead
    // would also drop stream and recording requests, leaving the player waiting forever.
    const int generation = ++m_generation;
    m_guideDays = guideDays;

    m_api->get(QStringLiteral("api/serverinfo"), {}, [this, generation](const ApiResult &result) {
        if (generation != m_generation) {
            return;
        }

        if (!result.ok) {
            Q_EMIT failed(result.error);
            return;
        }

        const QJsonObject info = result.json().object();
        Q_EMIT serverInfo(info.value(QLatin1String("name")).toString(QStringLiteral("Tvheadend")),
                          info.value(QLatin1String("sw_version")).toString());
        fetchChannels(generation);
    });
}

void TvheadendSource::fetchChannels(int generation)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("start"), QStringLiteral("0"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("100000"));
    m_api->get(
        QStringLiteral("api/channel/grid"),
        query,
        [this, generation](const ApiResult &result) {
            if (generation != m_generation) {
                return;
            }

            if (!result.ok) {
                Q_EMIT failed(tr("Loading channels failed: %1").arg(result.error));
                return;
            }

            QVector<Channel> channels;
            for (const QJsonValue &value : entriesOf(result.json())) {
                const QJsonObject object = value.toObject();
                if (!object.value(QLatin1String("enabled")).toBool(true)) {
                    continue;
                }

                Channel channel = Channel::fromJson(object);

                // Icons are relative to the server ("imagecache/12").
                if (!channel.icon.isEmpty() && !channel.icon.startsWith(QLatin1String("file://"))
                    && !channel.icon.startsWith(QLatin1Char('/'))) {
                    channel.icon = m_server.baseUrl().resolved(QUrl(channel.icon)).toString();
                }

                channels.append(channel);
            }

            std::stable_sort(channels.begin(),
                             channels.end(),
                             [](const Channel &a, const Channel &b) {
                                 // Unnumbered channels (0) go to the end, alphabetically.
                                 if ((a.number == 0) != (b.number == 0)) {
                                     return a.number != 0;
                                 }

                                 if (a.number != b.number) {
                                     return a.number < b.number;
                                 }

                                 return a.name.localeAwareCompare(b.name) < 0;
                             });

            Q_EMIT channelsReady(channels);
            m_incoming.clear();
            fetchEpgPage(generation, 0);
        });
}

void TvheadendSource::fetchEpgPage(int generation, int offset)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const qint64 end = now + qint64(m_guideDays) * 24 * 3600;
    const QJsonArray filter{
        QJsonObject{{QStringLiteral("field"), QStringLiteral("stop")},
                    {QStringLiteral("type"), QStringLiteral("numeric")},
                    {QStringLiteral("comparison"), QStringLiteral("gt")},
                    {QStringLiteral("value"), now}},
        QJsonObject{{QStringLiteral("field"), QStringLiteral("start")},
                    {QStringLiteral("type"), QStringLiteral("numeric")},
                    {QStringLiteral("comparison"), QStringLiteral("lt")},
                    {QStringLiteral("value"), end}},
    };

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("start"), QString::number(offset));
    form.addQueryItem(QStringLiteral("limit"), QString::number(s_epgPageSize));
    form.addQueryItem(QStringLiteral("sort"), QStringLiteral("start"));
    form.addQueryItem(QStringLiteral("dir"), QStringLiteral("ASC"));
    form.addQueryItem(QStringLiteral("filter"),
                      QString::fromUtf8(QJsonDocument(filter).toJson(QJsonDocument::Compact)));

    m_api->post(QStringLiteral("api/epg/events/grid"),
                form,
                [this, generation, offset, now, end](const ApiResult &result) {
                    if (generation != m_generation) {
                        return;
                    }

                    if (!result.ok) {
                        Q_EMIT guideFailed(tr("Loading the TV guide failed: %1").arg(result.error));
                        return;
                    }

                    const QJsonDocument document = result.json();
                    const QJsonArray entries = entriesOf(document);
                    for (const QJsonValue &value : entries) {
                        EpgEvent event = EpgEvent::fromJson(value.toObject());
                        // Filter client side as well in case the server ignored the filter.
                        if (event.stop > now && event.start < end) {
                            m_incoming.append(std::move(event));
                        }
                    }

                    const int total =
                        document.object().value(QLatin1String("totalCount")).toInt(-1);
                    const int fetched = offset + int(entries.size());
                    const bool more = entries.size() == s_epgPageSize
                        && (total < 0 || fetched < total) && fetched < s_maxEpgEvents;
                    if (more) {
                        fetchEpgPage(generation, fetched);
                    } else {
                        Q_EMIT guideReady(std::exchange(m_incoming, {}));
                    }
                });
}

QUrl TvheadendSource::streamUrl(const QString &channelId) const
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("profile"), AppSettings::instance()->streamProfile());
    QUrl url = m_server.url(QStringLiteral("stream/channel/") + channelId);
    url.setQuery(query);

    return url;
}

void TvheadendSource::requestStream(const QString &channelId)
{
    const QUrl direct = streamUrl(channelId);
    if (!direct.isValid() || direct.isEmpty()) {
        Q_EMIT streamFailed(channelId, tr("There is no stream address for this channel"));
        return;
    }

    // No ticket: it is only valid once, so the relay couldn't reconnect with it. The relay
    // answers the server's challenges with our credentials instead.
    Q_EMIT streamReady(
        channelId,
        {.url = direct, .username = m_server.username, .password = m_server.password});
}

void TvheadendSource::test()
{
    if (!m_server.isValid()) {
        Q_EMIT testFinished(false, tr("Please enter a server address"));
        return;
    }

    m_api->probe(m_server, QStringLiteral("api/serverinfo"), [this](const ApiResult &result) {
        if (!result.ok) {
            Q_EMIT testFinished(false, result.error);
            return;
        }

        const QJsonObject info = result.json().object();
        if (!info.contains(QLatin1String("api_version"))) {
            Q_EMIT testFinished(false, tr("The server answered, but does not look like TVHeadend"));
            return;
        }

        Q_EMIT testFinished(
            true,
            tr("Connected to %1 %2")
                .arg(info.value(QLatin1String("name")).toString(QStringLiteral("Tvheadend")),
                     info.value(QLatin1String("sw_version")).toString()));
    });
}

void TvheadendSource::record(const EpgEvent &event)
{
    const QString title = event.title;
    const qint64 id = event.id;
    const bool running = event.start <= QDateTime::currentSecsSinceEpoch();
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("event_id"), QString::number(id));
    form.addQueryItem(QStringLiteral("config_uuid"), QString());
    m_api->post(QStringLiteral("api/dvr/entry/create_by_event"),
                form,
                [this, id, title, running](const ApiResult &result) {
                    if (!result.ok) {
                        Q_EMIT notification(
                            tr("Could not schedule recording: %1").arg(result.error));
                        return;
                    }

                    Q_EMIT recordingChanged(id,
                                            firstUuid(result.json()),
                                            running ? QStringLiteral("recording")
                                                    : QStringLiteral("scheduled"));
                    Q_EMIT notification(tr("Recording scheduled: %1").arg(title));
                });
}

void TvheadendSource::cancelRecording(const EpgEvent &event)
{
    const qint64 id = event.id;
    const QString title = event.title;
    const bool running = event.dvrState == QLatin1String("recording");
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("uuid"), event.dvrUuid);
    const QString path =
        running ? QStringLiteral("api/dvr/entry/stop") : QStringLiteral("api/dvr/entry/cancel");
    m_api->post(path, form, [this, id, title, running](const ApiResult &result) {
        if (!result.ok) {
            Q_EMIT notification(tr("Could not cancel recording: %1").arg(result.error));
            return;
        }

        Q_EMIT recordingChanged(id, {}, {});
        Q_EMIT notification(running ? tr("Recording stopped: %1").arg(title)
                                    : tr("Recording cancelled: %1").arg(title));
    });
}
