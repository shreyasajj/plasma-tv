// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "stalkersource.h"

#include "tvhapi.h"

#include <QCryptographicHash>
#include <QTimeZone>

namespace
{
// MAG boxes report in every few minutes; many portals drop the token otherwise.
constexpr int s_watchdogMs = 2 * 60 * 1000;
}

StalkerSource::StalkerSource(const SourceConfig &config, QObject *parent)
    : Source(config, parent)
    , m_api(new TvhApi(this))
    , m_root(Stalker::portalRoot(config.field(QStringLiteral("portal"))))
{
    m_device.mac = Stalker::normalizeMac(config.secret(QStringLiteral("mac")));
    const QString model = config.field(QStringLiteral("stbModel")).trimmed();

    if (!model.isEmpty()) {
        m_device.model = model;
    }

    m_device.serial = config.secret(QStringLiteral("serial"));
    m_device.deviceId = config.secret(QStringLiteral("deviceId"));
    m_device.deviceId2 = config.secret(QStringLiteral("deviceId2"));
    m_device.signature = config.secret(QStringLiteral("signature"));
    m_device.login = config.field(QStringLiteral("username"));
    m_device.password = config.secret(QStringLiteral("password"));
    m_device.timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    setToken({});

    m_watchdog.setInterval(s_watchdogMs);
    connect(&m_watchdog, &QTimer::timeout, this, [this]() {
        if (!m_token.isEmpty() && m_endpoint.isValid()) {
            m_api->get(Stalker::watchdogUrl(m_endpoint), [](const ApiResult &) { });
        }
    });
}

QString StalkerSource::identity() const
{
    // Goes to the cache, where the MAC address has no business: its hash tells boxes apart
    // just as well.
    const QByteArray box =
        QCryptographicHash::hash(m_device.mac.toUtf8(), QCryptographicHash::Sha256).toHex();

    return QStringLiteral("stalker:") + m_root.toString() + QLatin1Char(' ')
        + QString::fromLatin1(box.left(16));
}

void StalkerSource::setToken(const QString &token)
{
    m_token = token;
    m_api->setHeaders(Stalker::headers(m_device, m_root, token));

    if (token.isEmpty()) {
        m_watchdog.stop();
    } else {
        m_watchdog.start();
    }
}

void StalkerSource::connectPortal(const Done &done)
{
    if (!m_root.isValid()) {
        done(tr("Please enter the portal address"));

        return;
    }

    if (m_device.mac.isEmpty()) {
        done(tr("Please enter the MAC address, e.g. 00:1A:79:12:34:56"));

        return;
    }

    if (!m_token.isEmpty()) {
        done({});

        return;
    }

    // Once a path answered, only that one is asked again.
    if (m_endpoint.isValid()) {
        const QList<QUrl> all = Stalker::endpoints(m_root);
        handshake(static_cast<int>(all.indexOf(m_endpoint)), done);
    } else {
        handshake(0, done);
    }
}

void StalkerSource::handshake(int endpoint, const Done &done)
{
    const QList<QUrl> all = Stalker::endpoints(m_root);
    const QUrl url = all.value(endpoint);

    m_api->get(Stalker::handshakeUrl(url),
               [this, endpoint, url, all, done](const ApiResult &result) {
                   const QString token = result.ok ? Stalker::parseToken(result.body) : QString();

                   if (token.isEmpty()) {
                       // Not this path: try the next one portals use.
                       if (!m_endpoint.isValid() && endpoint + 1 < all.size()) {
                           handshake(endpoint + 1, done);

                           return;
                       }

                       done(result.ok
                                ? tr("The server answered, but does not look like a Stalker portal")
                                : result.error);

                       return;
                   }

                   m_endpoint = url;
                   setToken(token);
                   signIn(done);
               });
}

void StalkerSource::signIn(const Done &done)
{
    m_api->get(Stalker::profileUrl(m_endpoint, m_device), [this, done](const ApiResult &result) {
        if (!result.ok || Stalker::isAuthFailure(result.httpStatus, result.body)
            || !Stalker::parseProfile(result.body)) {
            setToken({});
            done(result.ok
                     ? tr("The portal does not accept this box (check the MAC address and the "
                          "optional fields)")
                     : result.error);

            return;
        }

        if (m_device.login.isEmpty()) {
            done({});

            return;
        }

        m_api->get(Stalker::authUrl(m_endpoint, m_device), [this, done](const ApiResult &auth) {
            if (!auth.ok || !Stalker::parseAuth(auth.body)) {
                setToken({});
                done(auth.ok ? tr("Wrong username or password") : auth.error);

                return;
            }

            done({});
        });
    });
}

void StalkerSource::get(const std::function<QUrl()> &url, const Reply &reply, bool retried)
{
    m_api->get(url(), [this, url, reply, retried](const ApiResult &result) {
        if (!Stalker::isAuthFailure(result.httpStatus, result.body)) {
            reply(result);

            return;
        }

        if (retried) {
            ApiResult failed = result;
            failed.ok = false;
            failed.error = tr("The portal refused the request: %1")
                               .arg(QString::fromUtf8(result.body).trimmed().left(200));
            reply(failed);

            return;
        }

        // The token expired: once more with a new one.
        setToken({});
        connectPortal([this, url, reply, result](const QString &error) {
            if (!error.isEmpty()) {
                ApiResult failed = result;
                failed.ok = false;
                failed.error = error;
                reply(failed);

                return;
            }

            get(url, reply, true);
        });
    });
}

void StalkerSource::restoreChannels(const QVector<Channel> &channels)
{
    for (const Channel &channel : channels) {
        m_cmds.insert(channel.uuid, channel.stream);
    }
}

void StalkerSource::refresh(int guideDays)
{
    const int generation = ++m_generation;

    connectPortal([this, generation, guideDays](const QString &error) {
        if (generation != m_generation) {
            return;
        }

        if (!error.isEmpty()) {
            Q_EMIT failed(error);

            return;
        }

        Q_EMIT serverInfo(tr("Stalker portal"), {});

        get(
            [this]() {
                return Stalker::channelsUrl(m_endpoint);
            },
            [this, generation, guideDays](const ApiResult &result) {
                if (generation != m_generation) {
                    return;
                }

                QVector<Channel> channels;

                if (!result.ok) {
                    Q_EMIT failed(tr("Loading channels failed: %1").arg(result.error));

                    return;
                }

                if (!Stalker::parseChannels(result.body, m_root, &channels)) {
                    Q_EMIT failed(tr("Loading channels failed: the reply is not a channel list"));

                    return;
                }

                m_cmds.clear();
                restoreChannels(channels);
                Q_EMIT channelsReady(channels);

                get(
                    [this, guideDays]() {
                        return Stalker::guideUrl(m_endpoint, guideDays * 24);
                    },
                    [this, generation](const ApiResult &guide) {
                        if (generation != m_generation) {
                            return;
                        }

                        QVector<EpgEvent> events;

                        if (!guide.ok || !Stalker::parseGuide(guide.body, &events)) {
                            Q_EMIT guideFailed(
                                tr("Loading the TV guide failed: %1")
                                    .arg(guide.ok ? tr("the reply is not a guide") : guide.error));

                            return;
                        }

                        Q_EMIT guideReady(events);
                    });
            });
    });
}

void StalkerSource::requestStream(const QString &channelId)
{
    const QString cmd = m_cmds.value(channelId);

    if (cmd.isEmpty()) {
        Q_EMIT streamFailed(channelId, tr("The portal has no stream for this channel"));

        return;
    }

    connectPortal([this, channelId, cmd](const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT streamFailed(channelId, error);

            return;
        }

        get(
            [this, cmd]() {
                return Stalker::linkUrl(m_endpoint, cmd);
            },
            [this, channelId](const ApiResult &result) {
                const QUrl url = result.ok ? Stalker::parseLink(result.body) : QUrl();

                if (!url.isValid()) {
                    Q_EMIT streamFailed(channelId,
                                        result.ok ? tr("The portal sent no stream address")
                                                  : result.error);

                    return;
                }

                // Some portals check the box on the stream too; the relay sends the same headers.
                Q_EMIT streamReady(
                    channelId,
                    {.url = url, .headers = Stalker::streamHeaders(m_device, m_root)});
            });
    });
}

void StalkerSource::test()
{
    // A fresh token, so the test shows what a new start would do.
    setToken({});
    m_endpoint = QUrl();

    connectPortal([this](const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT testFinished(false, error);

            return;
        }

        get(
            [this]() {
                return Stalker::channelsUrl(m_endpoint);
            },
            [this](const ApiResult &result) {
                QVector<Channel> channels;

                if (!result.ok || !Stalker::parseChannels(result.body, m_root, &channels)) {
                    Q_EMIT testFinished(false,
                                        result.ok
                                            ? tr("Signed in, but the portal sent no channel list")
                                            : result.error);

                    return;
                }

                Q_EMIT testFinished(
                    true,
                    tr("Signed in to the portal: %1 channels").arg(channels.size()));
            });
    });
}
