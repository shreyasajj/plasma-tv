// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "bridgecontrol.h"
#include "channelmodel.h"
#include "epgstore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>

// Exercises the public protocol with an in-memory guide and a fake player.
class BridgeControlTest : public QObject
{
    Q_OBJECT

    EpgStore m_store;
    ChannelModel m_channels{&m_store};
    BridgeControl m_control{&m_channels};
    int m_serial = 0;

    QJsonObject request(const QString &command, QJsonObject fields = {})
    {
        fields.insert(QStringLiteral("id"), QString::number(++m_serial));
        fields.insert(QStringLiteral("command"), command);

        return QJsonDocument::fromJson(
                   m_control.Execute(QString::fromUtf8(QJsonDocument(fields).toJson())).toUtf8())
            .object();
    }

private Q_SLOTS:
    void init()
    {
        m_store.clear();
        m_channels.setChannels({});
        m_control.setReady(true);
        m_control.publish({}, QStringLiteral("idle"));
    }

    void excludesSecretsAndAmbiguity()
    {
        m_channels.setChannels({
            {.uuid = QStringLiteral("source:tvh-uuid"),
             .name = QStringLiteral("News"),
             .icon = QStringLiteral("https://user:password@host/secret/logo")},
            {.uuid = QStringLiteral("source:duplicate#2"),
             .name = QStringLiteral("Ambiguous"),
             .stream = QStringLiteral("https://provider/password/stream")},
            {.uuid = QStringLiteral("source:legacy"),
             .name = QStringLiteral("Sports"),
             .stream = QStringLiteral("https://host/secret"),
             .controlId = QStringLiteral("source:m3u-stable")},
        });
        const auto response = request(QStringLiteral("list_channels"));
        const QByteArray serialized = QJsonDocument(response).toJson();
        const auto items = response.value(QStringLiteral("result"))
                               .toObject()
                               .value(QStringLiteral("items"))
                               .toArray();
        QCOMPARE(items.size(), 2);
        QVERIFY(!serialized.contains("password"));
        QVERIFY(!serialized.contains("secret"));
        QVERIFY(!serialized.contains("legacy"));
        QVERIFY(!serialized.contains("tvh-uuid"));
        QVERIFY(!serialized.contains("Ambiguous"));
        QVERIFY(items.at(0)
                    .toObject()
                    .value(QStringLiteral("id"))
                    .toString()
                    .startsWith(QLatin1String("channel-")));
    }

    void selectionAndReplay()
    {
        const Channel entry{.uuid = QStringLiteral("s:tvh"), .name = QStringLiteral("News")};
        m_channels.setChannels({entry});
        QSignalSpy spy(&m_control, &BridgeControl::requested);
        const auto connection = connect(&m_control,
                                        &BridgeControl::requested,
                                        this,
                                        [this](const QString &, const QString &uuid) {
                                            m_control.publish(uuid, QStringLiteral("buffering"));
                                            m_control.acknowledge(true);
                                        });
        const QString payload = QString::fromUtf8(
            QJsonDocument(
                QJsonObject{{QStringLiteral("id"), QStringLiteral("replay")},
                            {QStringLiteral("command"), QStringLiteral("select_channel")},
                            {QStringLiteral("channel_id"), BridgeControl::channelId(entry)}})
                .toJson());
        const QString first = m_control.Execute(payload);
        QVERIFY(
            QJsonDocument::fromJson(first.toUtf8()).object().value(QStringLiteral("ok")).toBool());
        QCOMPARE(m_control.Execute(payload), first);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(1).toString(), entry.uuid);
        const auto unknown = request(QStringLiteral("select_channel"),
                                     {{QStringLiteral("channel_id"), QStringLiteral("unknown")}});
        QVERIFY(!unknown.value(QStringLiteral("ok")).toBool());
        QCOMPARE(spy.size(), 1);
        disconnect(connection);
    }

    void preservesFocusStateAndNegotiatesFeatures()
    {
        QSignalSpy focus(&m_control, &BridgeControl::focusRequested);
        m_control.publish(QStringLiteral("s:current"), QStringLiteral("playing"));
        QVERIFY(request(QStringLiteral("focus")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(focus.size(), 1);
        auto state =
            request(QStringLiteral("get_state")).value(QStringLiteral("result")).toObject();
        QCOMPARE(state.value(QStringLiteral("media"))
                     .toObject()
                     .value(QStringLiteral("state"))
                     .toString(),
                 QStringLiteral("playing"));
        QCOMPARE(state.value(QStringLiteral("capabilities")).toArray(),
                 QJsonArray({QStringLiteral("stop"), QStringLiteral("pause")}));
        QVERIFY(!request(QStringLiteral("seek")).value(QStringLiteral("ok")).toBool());
        QVERIFY(!request(QStringLiteral("pause")).value(QStringLiteral("ok")).toBool());
        m_control.setReady(false);
        QVERIFY(!request(QStringLiteral("list_channels")).value(QStringLiteral("ok")).toBool());
    }

    void announcesOnlyRealChanges()
    {
        m_control.setReady(true);
        m_control.publish({}, QStringLiteral("idle"));
        QSignalSpy changes(&m_control, &BridgeControl::StateChanged);
        m_control.publish({}, QStringLiteral("idle"));
        m_control.setReady(true);
        QCOMPARE(changes.size(), 0);
        m_control.publish(QStringLiteral("s:current"), QStringLiteral("buffering"));
        m_control.publish(QStringLiteral("s:current"), QStringLiteral("playing"));
        m_control.publish(QStringLiteral("s:current"), QStringLiteral("playing"));
        QCOMPARE(changes.size(), 2);
        m_control.publish(QStringLiteral("s:current"), QStringLiteral("unknown"));
        m_control.publish({}, QStringLiteral("idle"), true);
        m_control.setReady(false);
        QCOMPARE(changes.size(), 5);
        m_control.setReady(true);
        m_control.publish({}, QStringLiteral("idle"));
    }

    void guideAndPagination()
    {
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        m_store.setEvents({
            {.id = 1,
             .channel = QStringLiteral("s:a"),
             .start = now - 60,
             .stop = now + 60,
             .title = QStringLiteral("Current")},
            {.id = 2,
             .channel = QStringLiteral("s:a"),
             .start = now + 60,
             .stop = now + 120,
             .title = QStringLiteral("Next")},
        });
        m_channels.setChannels({{.uuid = QStringLiteral("s:a"), .name = QStringLiteral("A")},
                                {.uuid = QStringLiteral("s:b"), .name = QStringLiteral("B")}});
        const auto result = request(QStringLiteral("list_channels"), {{QStringLiteral("limit"), 1}})
                                .value(QStringLiteral("result"))
                                .toObject();
        QCOMPARE(result.value(QStringLiteral("next_offset")).toInt(), 1);
        QCOMPARE(result.value(QStringLiteral("total")).toInt(), 2);
        const auto item = result.value(QStringLiteral("items")).toArray().at(0).toObject();
        QCOMPARE(item.value(QStringLiteral("current"))
                     .toObject()
                     .value(QStringLiteral("title"))
                     .toString(),
                 QStringLiteral("Current"));
        QCOMPARE(
            item.value(QStringLiteral("next")).toObject().value(QStringLiteral("stop")).toDouble(),
            static_cast<double>(now + 120));
        const auto last = request(QStringLiteral("list_channels"), {{QStringLiteral("offset"), 1}})
                              .value(QStringLiteral("result"))
                              .toObject();
        QVERIFY(last.value(QStringLiteral("next_offset")).isNull());
        QCOMPARE(last.value(QStringLiteral("items")).toArray().size(), 1);
    }

    void retriesSelectionAfterCatalogueReadiness()
    {
        const Channel entry{.uuid = QStringLiteral("s:cold"),
                            .name = QStringLiteral("Cold launch")};
        const QString payload = QString::fromUtf8(
            QJsonDocument(
                QJsonObject{{QStringLiteral("id"), QStringLiteral("cold-selection")},
                            {QStringLiteral("command"), QStringLiteral("select_channel")},
                            {QStringLiteral("channel_id"), BridgeControl::channelId(entry)}})
                .toJson());
        m_control.publish({}, QStringLiteral("idle"), true);
        const auto pending = QJsonDocument::fromJson(m_control.Execute(payload).toUtf8()).object();
        QCOMPARE(pending.value(QStringLiteral("error"))
                     .toObject()
                     .value(QStringLiteral("code"))
                     .toString(),
                 QStringLiteral("not_ready"));
        m_channels.setChannels({entry});
        m_control.publish({}, QStringLiteral("idle"), false);
        const auto connection = connect(&m_control,
                                        &BridgeControl::requested,
                                        this,
                                        [this](const QString &, const QString &) {
                                            m_control.acknowledge(true);
                                        });
        QVERIFY(QJsonDocument::fromJson(m_control.Execute(payload).toUtf8())
                    .object()
                    .value(QStringLiteral("ok"))
                    .toBool());
        disconnect(connection);
    }

    void rejectsMalformedRequests()
    {
        for (const QString &payload : {QStringLiteral("[]"),
                                       QStringLiteral("{}"),
                                       QStringLiteral("not-json"),
                                       QString(20000, QLatin1Char('x'))}) {
            QVERIFY(!QJsonDocument::fromJson(m_control.Execute(payload).toUtf8())
                         .object()
                         .value(QStringLiteral("ok"))
                         .toBool());
        }

        for (const QJsonObject &fields :
             {QJsonObject{{QStringLiteral("limit"), true}},
              QJsonObject{{QStringLiteral("offset"), -1}},
              QJsonObject{{QStringLiteral("limit"), 51}},
              QJsonObject{{QStringLiteral("offset"), 0.5}},
              QJsonObject{{QStringLiteral("url"), QStringLiteral("https://example.org")}}}) {
            QVERIFY(!request(QStringLiteral("list_channels"), fields)
                         .value(QStringLiteral("ok"))
                         .toBool());
        }
    }
};

QTEST_GUILESS_MAIN(BridgeControlTest)
#include "test_bridgecontrol.moc"
