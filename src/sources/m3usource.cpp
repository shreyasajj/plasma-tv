// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "m3usource.h"

#include "m3u.h"
#include "tvhapi.h"

#include <QDateTime>

M3uSource::M3uSource(const SourceConfig &config, QObject *parent)
    : Source(config, parent)
    , m_api(new TvhApi(this))
    , m_server(config.serverConfig())
    , m_headers(HttpHeaders::parse(config.field(QStringLiteral("userAgent")),
                                   config.field(QStringLiteral("headers"))))
{
    m_api->setServer(m_server);
    m_api->setHeaders(m_headers);
}

QString M3uSource::identity() const
{
    return m_server.identity();
}

void M3uSource::restoreChannels(const QVector<Channel> &channels)
{
    for (const Channel &channel : channels) {
        m_streams.insert(channel.uuid, channel.stream);
    }
}

void M3uSource::refresh(int guideDays)
{
    const int generation = ++m_generation;
    m_guideDays = guideDays;

    m_api->get(m_server.playlist(), [this, generation](const ApiResult &result) {
        if (generation != m_generation) {
            return;
        }

        if (!result.ok) {
            Q_EMIT failed(tr("Loading the playlist failed: %1").arg(result.error));

            return;
        }

        M3u::Playlist playlist = M3u::parse(result.body, m_server.playlist());

        if (playlist.channels.isEmpty()) {
            Q_EMIT failed(M3u::looksLikePlaylist(result.body)
                              ? tr("The playlist contains no channels")
                              : tr("The address does not point to an M3U playlist"));

            return;
        }

        m_streams.clear();

        for (const Channel &channel : std::as_const(playlist.channels)) {
            m_streams.insert(channel.uuid, channel.stream);
        }

        Q_EMIT serverInfo(tr("M3U playlist"), {});
        Q_EMIT channelsReady(playlist.channels);

        const QUrl guide = m_server.xmltv().isValid() ? m_server.xmltv() : playlist.guideUrl;

        if (!guide.isValid()) {
            Q_EMIT guideReady({}); // no guide: channels only

            return;
        }

        fetchGuide(generation, guide, playlist.guideMap);
    });
}

void M3uSource::fetchGuide(int generation, const QUrl &url, const Xmltv::ChannelMap &map)
{
    m_api->get(url, [this, generation, map](const ApiResult &result) {
        if (generation != m_generation) {
            return;
        }

        if (!result.ok) {
            Q_EMIT guideFailed(tr("Loading the TV guide failed: %1").arg(result.error));

            return;
        }

        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const qint64 end = now + (static_cast<qint64>(m_guideDays) * 24 * 3600);

        // Guides of large playlists are tens of megabytes; keep the UI responsive.
        Xmltv::parseInThread(
            this,
            result.body,
            map,
            now,
            end,
            [this, generation](const QVector<EpgEvent> &events, const QString &error) {
                if (generation != m_generation) {
                    return;
                }

                if (!error.isEmpty()) {
                    Q_EMIT guideFailed(tr("Loading the TV guide failed: %1").arg(error));

                    return;
                }

                Q_EMIT guideReady(events);
            });
    });
}

void M3uSource::requestStream(const QString &channelId)
{
    const QUrl url(m_streams.value(channelId));

    if (!url.isValid() || url.isEmpty()) {
        Q_EMIT streamFailed(channelId, tr("The playlist has no stream address for this channel"));

        return;
    }

    // The player can't answer authentication challenges and would log credentials in the
    // URL, so streams from the playlist's own server go through the local relay.
    if (!m_server.username.isEmpty() && url.userName().isEmpty()
        && url.host() == m_server.baseUrl().host()) {
        Q_EMIT streamReady(channelId,
                           {.url = url,
                            .username = m_server.username,
                            .password = m_server.password,
                            .headers = m_headers});
        return;
    }

    Q_EMIT streamReady(channelId, {.url = url, .headers = m_headers});
}

void M3uSource::test()
{
    if (!m_server.playlist().isValid()) {
        Q_EMIT testFinished(false, tr("Please enter a playlist address"));

        return;
    }

    if (!m_server.xmltvUrl.isEmpty() && !m_server.xmltv().isValid()) {
        Q_EMIT testFinished(false, tr("The guide address is not valid"));

        return;
    }

    m_api->probe(m_server, m_server.playlist(), [this](const ApiResult &result) {
        if (!result.ok) {
            Q_EMIT testFinished(false, result.error);

            return;
        }

        if (!M3u::looksLikePlaylist(result.body)) {
            Q_EMIT testFinished(false, tr("The server answered, but did not send an M3U playlist"));

            return;
        }

        const M3u::Playlist playlist = M3u::parse(result.body, m_server.playlist());

        if (playlist.channels.isEmpty()) {
            Q_EMIT testFinished(false, tr("The playlist contains no channels"));

            return;
        }

        if (!m_server.xmltv().isValid() && !playlist.guideUrl.isValid()) {
            Q_EMIT testFinished(true,
                                tr("Playlist found: %1 channels, but no guide address")
                                    .arg(playlist.channels.size()));

            return;
        }

        Q_EMIT testFinished(true, tr("Playlist found: %1 channels").arg(playlist.channels.size()));
    });
}
