/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef BRIDGECONTROL_H
#define BRIDGECONTROL_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QQueue>

class ChannelModel;
struct Channel;

// Narrow local control endpoint. Main.qml acknowledges fixed player operations;
// the channel model supplies a public catalogue without streams or credentials.
class BridgeControl : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.jclsn.Flimmer.Control")

public:
    explicit BridgeControl(ChannelModel *channels, QObject *parent = nullptr);
    static QString serviceName();
    static QString objectPath();
    static QString channelId(const Channel &channel);
    Q_INVOKABLE void setReady(bool ready);
    Q_INVOKABLE void
    publish(const QString &channelUuid, const QString &state, bool catalogueLoading = false);
    Q_INVOKABLE void acknowledge(bool accepted);
    // A slot so main.cpp's ExportAllSlots puts it on the session bus.
    Q_SLOT QString Execute(const QString &payload);

Q_SIGNALS:
    // Exported on the session bus so listeners need not poll; carries no data.
    Q_SCRIPTABLE void StateChanged();
    void requested(const QString &command, const QString &channelUuid);
    void focusRequested();

private:
    struct Cached {
        QJsonObject request;
        QString response;
    };

    ChannelModel *m_channels;
    bool m_ready = false;
    bool m_acknowledged = false;
    bool m_catalogueLoading = false;
    QString m_channelUuid;
    QString m_state = QStringLiteral("idle");
    QHash<QString, Cached> m_cache;
    QQueue<QString> m_order;

    [[nodiscard]] QJsonObject channel(int row) const;
    [[nodiscard]] QJsonObject snapshot() const;
    [[nodiscard]] QJsonObject listChannels(const QJsonObject &request) const;
    QJsonObject playerCommand(const QJsonObject &request);
    QJsonObject execute(const QJsonObject &request);
};

#endif // BRIDGECONTROL_H
