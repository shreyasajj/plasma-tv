// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "bridgecontrol.h"

#include "channelmodel.h"
#include "epgstore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace
{
// Bound local messages and the replay cache even for untrusted callers.
constexpr int s_maxRequestBytes = 16384;
// Keep enough completed requests for clients to retry without repeating actions.
constexpr int s_maxCachedRequests = 128;
// Large provider catalogues are read one bounded page at a time.
constexpr int s_maxPageSize = 50;

QJsonObject failure(const QString &code, const QString &message)
{
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("error"),
             QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}}}};
}

QJsonObject success(const QJsonObject &result)
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("result"), result}};
}

bool boundedInteger(const QJsonValue &value, int maximum)
{
    const double number = value.toDouble(-1);

    return value.isDouble() && std::isfinite(number) && number >= 0 && number <= maximum
        && std::floor(number) == number;
}
}

BridgeControl::BridgeControl(ChannelModel *channels, QObject *parent)
    : QObject(parent)
    , m_channels(channels)
{
}

QString BridgeControl::serviceName()
{
    return QStringLiteral("io.github.jclsn.Flimmer.Control");
}

QString BridgeControl::objectPath()
{
    return QStringLiteral("/io/github/jclsn/Flimmer/Control");
}

QString BridgeControl::channelId(const Channel &channel)
{
    // M3U channels must have an unambiguous identity assigned by the parser.
    if (channel.controlId.isEmpty() && !channel.stream.isEmpty()) {
        return {};
    }

    const QString identity = channel.controlId.isEmpty()
        ? QStringLiteral("provider:") + channel.uuid
        : QStringLiteral("playlist:") + channel.controlId;

    if (channel.uuid.isEmpty()) {
        return {};
    }

    return QStringLiteral("channel-")
        + QString::fromLatin1(
               QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QJsonObject BridgeControl::channel(int row) const
{
    const Channel &entry = m_channels->channels().at(row);
    const QVariantMap guide = m_channels->get(row);
    // Artwork URLs may embed provider secrets in their path, not only the query.
    // Omit them until an authenticated image proxy can serve the bytes safely.
    return {
        {QStringLiteral("id"), channelId(entry)},
        {QStringLiteral("name"), entry.name.left(512)},
        {QStringLiteral("number"), entry.number},
        {QStringLiteral("current"),
         QJsonObject{
             {QStringLiteral("title"),
              guide.value(QStringLiteral("nowTitle")).toString().left(2048)},
             {QStringLiteral("start"), guide.value(QStringLiteral("nowStart")).toDouble() / 1000.0},
             {QStringLiteral("stop"), guide.value(QStringLiteral("nowStop")).toDouble() / 1000.0}}},
        {QStringLiteral("next"),
         QJsonObject{{QStringLiteral("title"),
                      guide.value(QStringLiteral("nextTitle")).toString().left(2048)},
                     {QStringLiteral("start"),
                      guide.value(QStringLiteral("nextStart")).toDouble() / 1000.0},
                     {QStringLiteral("stop"),
                      guide.value(QStringLiteral("nextStop")).toDouble() / 1000.0}}}};
}

QJsonObject BridgeControl::snapshot() const
{
    QJsonObject media{{QStringLiteral("state"), m_state},
                      {QStringLiteral("updated_at"),
                       QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    const int row = m_channels->indexOfUuid(m_channelUuid);

    if (row >= 0) {
        media.insert(QStringLiteral("channel"), channel(row));
    }

    QJsonArray capabilities;

    if (m_ready && !m_channelUuid.isEmpty()) {
        capabilities.append(QStringLiteral("stop"));

        if (m_state == QLatin1String("playing")) {
            capabilities.append(QStringLiteral("pause"));
        } else if (m_state == QLatin1String("paused")) {
            capabilities.append(QStringLiteral("play"));
        }
    }

    return {{QStringLiteral("protocol"), 1},
            {QStringLiteral("ready"), m_ready},
            {QStringLiteral("content_types"), QJsonArray{QStringLiteral("channel")}},
            {QStringLiteral("catalogue_loading"), m_catalogueLoading},
            {QStringLiteral("capabilities"), capabilities},
            {QStringLiteral("media"), media}};
}

void BridgeControl::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    Q_EMIT StateChanged();
}

void BridgeControl::publish(const QString &channelUuid, const QString &state, bool catalogueLoading)
{
    const QStringList states{QStringLiteral("idle"),
                             QStringLiteral("playing"),
                             QStringLiteral("paused"),
                             QStringLiteral("buffering"),
                             QStringLiteral("error")};
    const QString next = states.contains(state) ? state : QStringLiteral("idle");
    const bool changed =
        m_catalogueLoading != catalogueLoading || m_channelUuid != channelUuid || m_state != next;
    m_catalogueLoading = catalogueLoading;
    m_channelUuid = channelUuid;
    m_state = next;
    if (changed) {
        Q_EMIT StateChanged();
    }
}

void BridgeControl::acknowledge(bool accepted)
{
    m_acknowledged = accepted;
}

QJsonObject BridgeControl::execute(const QJsonObject &request)
{
    const QString command = request.value(QStringLiteral("command")).toString();
    QStringList allowed{QStringLiteral("id"), QStringLiteral("command")};

    if (command == QLatin1String("select_channel")) {
        allowed << QStringLiteral("channel_id");
    } else if (command == QLatin1String("list_channels")) {
        allowed << QStringLiteral("offset") << QStringLiteral("limit");
    } else if (!QStringList{QStringLiteral("get_state"),
                            QStringLiteral("focus"),
                            QStringLiteral("play"),
                            QStringLiteral("pause"),
                            QStringLiteral("stop")}
                    .contains(command)) {
        return failure(QStringLiteral("unsupported"), QStringLiteral("Unsupported command"));
    }

    for (auto it = request.begin(); it != request.end(); ++it) {
        if (!allowed.contains(it.key())) {
            return failure(QStringLiteral("invalid_request"),
                           QStringLiteral("Unknown request fields"));
        }
    }

    if (command == QLatin1String("get_state")) {
        return success(snapshot());
    }

    if (command == QLatin1String("focus")) {
        Q_EMIT focusRequested();

        return success({{QStringLiteral("delivery"), QStringLiteral("focus_requested")}});
    }

    if (!m_ready) {
        return failure(QStringLiteral("not_ready"), QStringLiteral("The application is not ready"));
    }

    if (command == QLatin1String("list_channels")) {
        return listChannels(request);
    }

    return playerCommand(request);
}

QJsonObject BridgeControl::listChannels(const QJsonObject &request) const
{
    const auto offsetValue = request.value(QStringLiteral("offset"));
    const auto limitValue = request.value(QStringLiteral("limit"));

    if ((!offsetValue.isUndefined() && !boundedInteger(offsetValue, 1000000))
        || (!limitValue.isUndefined()
            && (!boundedInteger(limitValue, s_maxPageSize) || limitValue.toInt() == 0))) {
        return failure(QStringLiteral("invalid_request"), QStringLiteral("Invalid page bounds"));
    }

    const int offset = offsetValue.toInt(0);
    const int limit = limitValue.toInt(s_maxPageSize);
    QJsonArray items;
    int total = 0;

    for (int row = 0; row < m_channels->rowCount(); ++row) {
        if (channelId(m_channels->channels().at(row)).isEmpty()) {
            continue;
        }

        if (total >= offset && items.size() < limit) {
            items.append(channel(row));
        }

        ++total;
    }

    const int next = offset + static_cast<int>(items.size());

    return success(
        {{QStringLiteral("items"), items},
         {QStringLiteral("loading"), m_catalogueLoading},
         {QStringLiteral("total"), total},
         {QStringLiteral("next_offset"), next < total ? QJsonValue(next) : QJsonValue()}});
}

QJsonObject BridgeControl::playerCommand(const QJsonObject &request)
{
    const QString command = request.value(QStringLiteral("command")).toString();

    QString uuid;

    if (command == QLatin1String("select_channel")) {
        const QString id = request.value(QStringLiteral("channel_id")).toString();

        if (id.isEmpty() || id.size() > 128) {
            return failure(QStringLiteral("invalid_request"),
                           QStringLiteral("A channel ID is required"));
        }

        for (const Channel &entry : m_channels->channels()) {
            if (channelId(entry) == id) {
                uuid = entry.uuid;
                break;
            }
        }

        if (uuid.isEmpty() && m_catalogueLoading) {
            return failure(QStringLiteral("not_ready"),
                           QStringLiteral("The channel catalogue is loading"));
        }

        if (uuid.isEmpty()) {
            return failure(QStringLiteral("not_found"),
                           QStringLiteral("Unknown or ambiguous channel ID"));
        }
    } else if (!snapshot().value(QStringLiteral("capabilities")).toArray().contains(command)) {
        return failure(QStringLiteral("unsupported"),
                       QStringLiteral("Player command is unavailable"));
    }

    m_acknowledged = false;
    Q_EMIT requested(command, uuid);

    if (!m_acknowledged) {
        return failure(QStringLiteral("not_ready"),
                       QStringLiteral("The player did not accept the command"));
    }

    return success({{QStringLiteral("delivery"), QStringLiteral("player_accepted")},
                    {QStringLiteral("media"), snapshot().value(QStringLiteral("media"))}});
}

QString BridgeControl::Execute(const QString &payload)
{
    const QByteArray bytes = payload.toUtf8();
    const QJsonDocument document =
        bytes.size() <= s_maxRequestBytes ? QJsonDocument::fromJson(bytes) : QJsonDocument();
    const QJsonObject request = document.object();
    const QString id = request.value(QStringLiteral("id")).toString();
    QJsonObject response;

    if (bytes.size() > s_maxRequestBytes || !document.isObject() || id.isEmpty()
        || id.size() > 128) {
        response = failure(QStringLiteral("invalid_request"),
                           QStringLiteral("A bounded JSON request and string ID are required"));
    } else if (m_cache.contains(id)) {
        const Cached &cached = m_cache[id];

        if (cached.request == request) {
            return cached.response;
        }

        response = failure(QStringLiteral("invalid_request"),
                           QStringLiteral("Request ID reused with different data"));
    } else {
        response = execute(request);
        response.insert(QStringLiteral("id"), id);
        const QString serialized =
            QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
        if (!response.value(QStringLiteral("ok")).toBool()
            && response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code"))
                == QLatin1String("not_ready")) {
            return serialized;
        }

        m_cache.insert(id, {.request = request, .response = serialized});
        m_order.enqueue(id);

        while (m_order.size() > s_maxCachedRequests) {
            m_cache.remove(m_order.dequeue());
        }

        return serialized;
    }

    response.insert(QStringLiteral("id"), id);

    return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
}
