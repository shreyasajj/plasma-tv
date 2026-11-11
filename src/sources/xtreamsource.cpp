// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "xtreamsource.h"

#include "tvhapi.h"

#include <QDateTime>
#include <QLocale>

XtreamSource::XtreamSource(const SourceConfig &config, QObject *parent)
    : Source(config, parent)
    , m_api(new TvhApi(this))
    , m_base(Xtream::baseUrl(config.field(QStringLiteral("server"))))
    , m_user(config.field(QStringLiteral("username")))
    , m_password(config.secret(QStringLiteral("password")))
    , m_headers(HttpHeaders::parse(config.field(QStringLiteral("userAgent")), {}))
{
    m_api->setHeaders(m_headers);
}

QString XtreamSource::identity() const
{
    // Another user on the same panel can have other channels.
    return QStringLiteral("xtream:") + m_base.toString() + QLatin1Char(' ') + m_user;
}

void XtreamSource::refresh(int guideDays)
{
    const int generation = ++m_generation;
    m_guideDays = guideDays;

    if (!m_base.isValid()) {
        Q_EMIT failed(tr("Please enter the server address"));

        return;
    }

    m_api->get(Xtream::apiUrl(m_base, m_user, m_password),
               [this, generation](const ApiResult &result) {
                   if (generation != m_generation) {
                       return;
                   }

                   if (!result.ok) {
                       Q_EMIT failed(result.error);

                       return;
                   }

                   if (!Xtream::parseLogin(result.body, &m_account)) {
                       Q_EMIT failed(
                           tr("The server answered, but does not look like an Xtream Codes panel"));

                       return;
                   }

                   const QString error =
                       Xtream::loginError(m_account, QDateTime::currentSecsSinceEpoch());

                   if (!error.isEmpty()) {
                       Q_EMIT failed(error);

                       return;
                   }

                   Q_EMIT serverInfo(tr("Xtream Codes"), {});
                   fetchStreams(generation);
               });
}

void XtreamSource::fetchStreams(int generation)
{
    m_api->get(Xtream::apiUrl(m_base, m_user, m_password, QStringLiteral("get_live_streams")),
               [this, generation](const ApiResult &result) {
                   if (generation != m_generation) {
                       return;
                   }

                   Xtream::Streams streams;

                   if (!result.ok) {
                       Q_EMIT failed(tr("Loading channels failed: %1").arg(result.error));

                       return;
                   }

                   if (!Xtream::parseStreams(result.body, &streams)) {
                       Q_EMIT failed(
                           tr("Loading channels failed: the reply is not a channel list"));

                       return;
                   }

                   Q_EMIT channelsReady(streams.channels);
                   fetchGuide(generation, streams.guideMap);
               });
}

void XtreamSource::fetchGuide(int generation, const Xmltv::ChannelMap &map)
{
    m_api->get(Xtream::guideUrl(m_base, m_user, m_password),
               [this, generation, map](const ApiResult &result) {
                   if (generation != m_generation) {
                       return;
                   }

                   if (!result.ok) {
                       Q_EMIT guideFailed(tr("The TV guide isn't available: %1").arg(result.error));

                       return;
                   }

                   const qint64 now = QDateTime::currentSecsSinceEpoch();
                   const qint64 end = now + (static_cast<qint64>(m_guideDays) * 24 * 3600);

                   // Like an M3U guide: large, so parsed on a thread of its own.
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
                               Q_EMIT guideFailed(
                                   tr("The TV guide isn't available: %1").arg(error));

                               return;
                           }

                           Q_EMIT guideReady(events);
                       });
               });
}

void XtreamSource::requestStream(const QString &channelId)
{
    if (!m_base.isValid()) {
        Q_EMIT streamFailed(channelId, tr("Please enter the server address"));

        return;
    }

    // The password is in the path: only the local relay ever sees this address.
    Q_EMIT streamReady(
        channelId,
        {.url = Xtream::streamUrl(m_base, m_user, m_password, channelId, m_account.formats),
         .headers = m_headers,
         .secretUrl = true});
}

void XtreamSource::explainStreamError(const QString &channelId)
{
    // Ask the panel how many connections are open now that ours has closed.
    m_api->get(Xtream::apiUrl(m_base, m_user, m_password),
               [this, channelId](const ApiResult &result) {
                   Xtream::Account account;

                   if (!result.ok || !Xtream::parseLogin(result.body, &account)) {
                       return;
                   }

                   const QString error =
                       Xtream::loginError(account, QDateTime::currentSecsSinceEpoch());

                   if (!error.isEmpty()) {
                       Q_EMIT streamFailed(channelId, error);
                   } else if (Xtream::inUse(account)) {
                       Q_EMIT streamFailed(channelId,
                                           tr("Your account is already in use on another device"));
                   }
               });
}

void XtreamSource::test()
{
    if (!m_base.isValid()) {
        Q_EMIT testFinished(false, tr("Please enter the server address"));

        return;
    }

    if (m_user.isEmpty()) {
        Q_EMIT testFinished(false, tr("Please enter the username"));

        return;
    }

    m_api->get(Xtream::apiUrl(m_base, m_user, m_password), [this](const ApiResult &result) {
        Xtream::Account account;

        if (!result.ok) {
            Q_EMIT testFinished(false, result.error);

            return;
        }

        if (!Xtream::parseLogin(result.body, &account)) {
            Q_EMIT testFinished(
                false,
                tr("The server answered, but does not look like an Xtream Codes panel"));

            return;
        }

        const QString error = Xtream::loginError(account, QDateTime::currentSecsSinceEpoch());

        if (!error.isEmpty()) {
            Q_EMIT testFinished(false, error);

            return;
        }

        const QString expiry = account.expires > 0
            ? tr(", valid until %1")
                  .arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(account.expires).date(),
                                          QLocale::ShortFormat))
            : QString();
        Q_EMIT testFinished(true, tr("Signed in%1").arg(expiry));
    });
}
