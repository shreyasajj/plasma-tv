// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "xtream.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QUrlQuery>

namespace
{
// Panels send numbers as numbers or as strings, and null for "none".
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

qint64 number(const QJsonValue &value)
{
    return text(value).toLongLong();
}

QString tr(const char *text)
{
    return QCoreApplication::translate("Xtream", text);
}
}

QUrl Xtream::baseUrl(const QString &server)
{
    QString trimmed = server.trimmed();

    // People paste the API address too.
    for (const QLatin1String suffix : {QLatin1String("player_api.php"), QLatin1String("get.php")}) {
        const qsizetype at = trimmed.indexOf(suffix);

        if (at >= 0) {
            trimmed.truncate(at);
        }
    }

    QUrl url = QUrl::fromUserInput(trimmed);

    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }

    url.setQuery(QString());
    url.setFragment(QString());

    if (!url.path().endsWith(QLatin1Char('/'))) {
        url.setPath(url.path() + QLatin1Char('/'));
    }

    return url;
}

QUrl Xtream::apiUrl(const QUrl &base,
                    const QString &user,
                    const QString &password,
                    const QString &action)
{
    QUrl url = base.resolved(QUrl(QStringLiteral("player_api.php")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("username"),
                       QString::fromLatin1(QUrl::toPercentEncoding(user)));
    query.addQueryItem(QStringLiteral("password"),
                       QString::fromLatin1(QUrl::toPercentEncoding(password)));

    if (!action.isEmpty()) {
        query.addQueryItem(QStringLiteral("action"), action);
    }

    url.setQuery(query);

    return url;
}

QUrl Xtream::guideUrl(const QUrl &base, const QString &user, const QString &password)
{
    QUrl url = base.resolved(QUrl(QStringLiteral("xmltv.php")));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("username"),
                       QString::fromLatin1(QUrl::toPercentEncoding(user)));
    query.addQueryItem(QStringLiteral("password"),
                       QString::fromLatin1(QUrl::toPercentEncoding(password)));
    url.setQuery(query);

    return url;
}

QUrl Xtream::streamUrl(const QUrl &base,
                       const QString &user,
                       const QString &password,
                       const QString &streamId,
                       const QStringList &formats)
{
    const bool hlsOnly = !formats.isEmpty() && !formats.contains(QLatin1String("ts"))
        && formats.contains(QLatin1String("m3u8"));
    const QString extension = hlsOnly ? QStringLiteral(".m3u8") : QStringLiteral(".ts");
    const QString path = QStringLiteral("live/%1/%2/%3%4")
                             .arg(QString::fromLatin1(QUrl::toPercentEncoding(user)),
                                  QString::fromLatin1(QUrl::toPercentEncoding(password)),
                                  streamId,
                                  extension);

    return base.resolved(QUrl(path, QUrl::StrictMode));
}

bool Xtream::parseLogin(const QByteArray &data, Account *account)
{
    const QJsonObject root = QJsonDocument::fromJson(data).object();

    if (!root.contains(QLatin1String("user_info"))) {
        return false;
    }

    const QJsonObject user = root.value(QLatin1String("user_info")).toObject();
    *account = {};
    account->auth = number(user.value(QLatin1String("auth"))) == 1;
    account->status = text(user.value(QLatin1String("status")));
    account->expires = number(user.value(QLatin1String("exp_date")));
    account->maxConnections =
        static_cast<int>(number(user.value(QLatin1String("max_connections"))));
    account->activeConnections = static_cast<int>(number(user.value(QLatin1String("active_cons"))));
    account->message = text(user.value(QLatin1String("message")));

    for (const auto &format : user.value(QLatin1String("allowed_output_formats")).toArray()) {
        account->formats.append(format.toString());
    }

    return true;
}

QString Xtream::loginError(const Account &account, qint64 now)
{
    if (!account.auth) {
        return tr("Wrong username or password");
    }

    if (account.expires > 0 && account.expires < now) {
        return tr("The account expired on %1")
            .arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(account.expires).date(),
                                    QLocale::ShortFormat));
    }

    if (!account.status.isEmpty()
        && account.status.compare(QLatin1String("Active"), Qt::CaseInsensitive) != 0) {
        return tr("The account is %1").arg(account.status.toLower());
    }

    return {};
}

bool Xtream::inUse(const Account &account)
{
    return account.maxConnections > 0 && account.activeConnections >= account.maxConnections;
}

bool Xtream::parseStreams(const QByteArray &data, Streams *streams)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);

    if (!document.isArray()) {
        return false;
    }

    *streams = {};

    for (const auto &value : document.array()) {
        const QJsonObject object = value.toObject();
        const QString id = text(object.value(QLatin1String("stream_id")));

        if (id.isEmpty()) {
            continue;
        }

        Channel channel;
        channel.uuid = id;
        channel.number = static_cast<int>(number(object.value(QLatin1String("num"))));
        channel.name = text(object.value(QLatin1String("name"))).trimmed();
        channel.icon = text(object.value(QLatin1String("stream_icon")));
        streams->channels.append(channel);

        const QString guideId =
            text(object.value(QLatin1String("epg_channel_id"))).trimmed().toLower();

        if (!guideId.isEmpty()) {
            streams->guideMap.byId[guideId].append(id);
        }

        if (!channel.name.isEmpty()) {
            streams->guideMap.byName[channel.name.toLower()].append(id);
        }
    }

    return true;
}
