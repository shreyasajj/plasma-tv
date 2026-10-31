// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "stalker.h"

#include "xmltv.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrlQuery>

namespace
{
using Params = QList<QPair<QString, QString>>;

QString text(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }

    if (value.isDouble()) {
        return QString::number(value.toInteger());
    }

    return {};
}

QJsonValue js(const QByteArray &data)
{
    return QJsonDocument::fromJson(data).object().value(QLatin1String("js"));
}

QUrl request(const QUrl &endpoint,
             const QString &type,
             const QString &action,
             const Params &params = {})
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("type"), type);
    query.addQueryItem(QStringLiteral("action"), action);

    for (const auto &[key, value] : params) {
        // Left out when empty: portals differ in what they check.
        if (!value.isEmpty()) {
            query.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
        }
    }

    query.addQueryItem(QStringLiteral("JsHttpRequest"), QStringLiteral("1-xml"));
    QUrl url = endpoint;
    url.setQuery(query);

    return url;
}

QString userAgent(const QString &model)
{
    return QStringLiteral(
               "Mozilla/5.0 (QtEmbedded; U; Linux; C) AppleWebKit/533.3 (KHTML, like Gecko) "
               "%1 stbapp ver: 2 rev: 250 Safari/533.3")
        .arg(model);
}

QByteArray cookie(const Stalker::Device &device)
{
    QByteArray value = "mac=" + QUrl::toPercentEncoding(device.mac) + "; stb_lang=en";

    if (!device.timezone.isEmpty()) {
        value += "; timezone=" + QUrl::toPercentEncoding(device.timezone);
    }

    return value;
}
}

QString Stalker::normalizeMac(const QString &mac)
{
    QString hex = mac.trimmed().toUpper();
    hex.remove(QRegularExpression(QStringLiteral("[:\\-. ]")));

    static const QRegularExpression valid(QStringLiteral("^[0-9A-F]{12}$"));

    if (!valid.match(hex).hasMatch()) {
        return {};
    }

    QStringList pairs;

    for (int i = 0; i < 12; i += 2) {
        pairs.append(hex.mid(i, 2));
    }

    return pairs.join(QLatin1Char(':'));
}

QUrl Stalker::portalRoot(const QString &address)
{
    QUrl url = QUrl::fromUserInput(address.trimmed());

    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }

    QString path = url.path();

    // People paste the web address of the portal, or one of its API addresses.
    for (const QLatin1String suffix : {QLatin1String("/server/load.php"),
                                       QLatin1String("/portal.php"),
                                       QLatin1String("/c/index.html"),
                                       QLatin1String("/c/"),
                                       QLatin1String("/c")}) {
        if (path.endsWith(suffix)) {
            path.chop(suffix.size() - 1); // keeps the slash
            break;
        }
    }

    if (!path.endsWith(QLatin1Char('/'))) {
        path += QLatin1Char('/');
    }

    url.setPath(path);
    url.setQuery(QString());
    url.setFragment(QString());

    return url;
}

QList<QUrl> Stalker::endpoints(const QUrl &root)
{
    return {root.resolved(QUrl(QStringLiteral("portal.php"))),
            root.resolved(QUrl(QStringLiteral("server/load.php"))),
            root.resolved(QUrl(QStringLiteral("stalker_portal/server/load.php")))};
}

Headers Stalker::headers(const Device &device, const QUrl &root, const QString &token)
{
    Headers headers = streamHeaders(device, root);
    headers.append({"Referer", root.resolved(QUrl(QStringLiteral("c/"))).toEncoded()});

    if (!token.isEmpty()) {
        headers.append({"Authorization", "Bearer " + token.toUtf8()});
    }

    return headers;
}

Headers Stalker::streamHeaders(const Device &device, const QUrl &)
{
    return {{"User-Agent", userAgent(device.model).toUtf8()},
            {"X-User-Agent", QStringLiteral("Model: %1; Link: WiFi").arg(device.model).toUtf8()},
            {"Cookie", cookie(device)}};
}

QUrl Stalker::handshakeUrl(const QUrl &endpoint)
{
    return request(endpoint,
                   QStringLiteral("stb"),
                   QStringLiteral("handshake"),
                   {{QStringLiteral("token"), {}}});
}

QUrl Stalker::profileUrl(const QUrl &endpoint, const Device &device)
{
    return request(endpoint,
                   QStringLiteral("stb"),
                   QStringLiteral("get_profile"),
                   {{QStringLiteral("hd"), QStringLiteral("1")},
                    {QStringLiteral("stb_type"), device.model},
                    {QStringLiteral("sn"), device.serial},
                    {QStringLiteral("device_id"), device.deviceId},
                    {QStringLiteral("device_id2"), device.deviceId2},
                    {QStringLiteral("signature"), device.signature},
                    {QStringLiteral("auth_second_step"), QStringLiteral("1")}});
}

QUrl Stalker::authUrl(const QUrl &endpoint, const Device &device)
{
    return request(
        endpoint,
        QStringLiteral("stb"),
        QStringLiteral("do_auth"),
        {{QStringLiteral("login"), device.login}, {QStringLiteral("password"), device.password}});
}

QUrl Stalker::channelsUrl(const QUrl &endpoint)
{
    return request(endpoint, QStringLiteral("itv"), QStringLiteral("get_all_channels"));
}

QUrl Stalker::guideUrl(const QUrl &endpoint, int hours)
{
    return request(endpoint,
                   QStringLiteral("itv"),
                   QStringLiteral("get_epg_info"),
                   {{QStringLiteral("period"), QString::number(hours)}});
}

QUrl Stalker::linkUrl(const QUrl &endpoint, const QString &cmd)
{
    return request(endpoint,
                   QStringLiteral("itv"),
                   QStringLiteral("create_link"),
                   {{QStringLiteral("cmd"), cmd},
                    {QStringLiteral("forced_storage"), QStringLiteral("0")},
                    {QStringLiteral("disable_ad"), QStringLiteral("0")}});
}

QUrl Stalker::watchdogUrl(const QUrl &endpoint)
{
    return request(endpoint,
                   QStringLiteral("watchdog"),
                   QStringLiteral("get_events"),
                   {{QStringLiteral("init"), QStringLiteral("0")},
                    {QStringLiteral("cur_play_type"), QStringLiteral("1")},
                    {QStringLiteral("event_active_id"), QStringLiteral("0")}});
}

bool Stalker::isAuthFailure(int httpStatus, const QByteArray &data)
{
    return httpStatus == 401 || data.contains("Authorization failed");
}

QString Stalker::parseToken(const QByteArray &data)
{
    return text(js(data).toObject().value(QLatin1String("token")));
}

bool Stalker::parseProfile(const QByteArray &data)
{
    return js(data).isObject();
}

bool Stalker::parseAuth(const QByteArray &data)
{
    const QJsonValue value = js(data);

    return value.isBool() ? value.toBool() : value.isObject();
}

bool Stalker::parseChannels(const QByteArray &data, const QUrl &root, QVector<Channel> *channels)
{
    const QJsonValue list = js(data).toObject().value(QLatin1String("data"));

    if (!list.isArray()) {
        return false;
    }

    channels->clear();

    for (const auto &value : list.toArray()) {
        const QJsonObject object = value.toObject();
        Channel channel;
        channel.uuid = text(object.value(QLatin1String("id")));
        channel.stream = text(object.value(QLatin1String("cmd")));

        if (channel.uuid.isEmpty() || channel.stream.isEmpty()) {
            continue;
        }

        channel.number = text(object.value(QLatin1String("number"))).toInt();
        channel.name = text(object.value(QLatin1String("name"))).trimmed();

        // Logos are often relative to the portal.
        const QString logo = text(object.value(QLatin1String("logo")));

        if (!logo.isEmpty()) {
            channel.icon = root.resolved(QUrl(logo)).toString();
        }

        channels->append(channel);
    }

    return true;
}

bool Stalker::parseGuide(const QByteArray &data, QVector<EpgEvent> *events)
{
    const QJsonValue value = js(data).toObject().value(QLatin1String("data"));

    if (!value.isObject() && !value.isArray()) {
        return false;
    }

    // Either { "<channel id>": [events] } or one list of events with their ch_id.
    QList<QPair<QString, QJsonArray>> groups;

    if (value.isObject()) {
        const QJsonObject byChannel = value.toObject();

        for (auto it = byChannel.begin(); it != byChannel.end(); ++it) {
            groups.append({it.key(), it.value().toArray()});
        }
    } else {
        groups.append({QString(), value.toArray()});
    }

    events->clear();

    for (const auto &[channel, list] : groups) {
        for (const auto &entry : list) {
            const QJsonObject object = entry.toObject();
            EpgEvent event;
            event.id = text(object.value(QLatin1String("id"))).toLongLong();
            event.channel =
                channel.isEmpty() ? text(object.value(QLatin1String("ch_id"))) : channel;
            event.start = text(object.value(QLatin1String("start_timestamp"))).toLongLong();
            event.stop = text(object.value(QLatin1String("stop_timestamp"))).toLongLong();
            event.title = text(object.value(QLatin1String("name")));
            event.description = text(object.value(QLatin1String("descr")));
            event.genre = Xmltv::genreFromCategory(text(object.value(QLatin1String("category"))));

            if (event.id == 0 || event.channel.isEmpty() || event.stop <= event.start) {
                continue;
            }

            events->append(event);
        }
    }

    return true;
}

QUrl Stalker::parseLink(const QByteArray &data)
{
    QString cmd = text(js(data).toObject().value(QLatin1String("cmd"))).trimmed();

    // "ffmpeg http://…" or "auto http://…": the player MAG boxes use, then the address.
    const qsizetype space = cmd.indexOf(QLatin1Char(' '));

    if (space > 0 && !cmd.left(space).contains(QLatin1String("://"))) {
        cmd = cmd.mid(space + 1).trimmed();
    }

    const QUrl url(cmd);

    return url.isValid() && !url.scheme().isEmpty() ? url : QUrl();
}
