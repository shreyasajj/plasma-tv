/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STALKERSOURCE_H
#define STALKERSOURCE_H

#include "source.h"
#include "stalker.h"

#include <QHash>
#include <QTimer>

#include <functional>

struct ApiResult;
class TvhApi;

// A Stalker portal (MAG boxes): the box is identified by its MAC address and a
// token from a handshake. Stream links are short-lived and requested on every
// channel change.
class StalkerSource : public Source
{
    Q_OBJECT

public:
    explicit StalkerSource(const SourceConfig &config, QObject *parent = nullptr);

    [[nodiscard]] QString identity() const override;

    void restoreChannels(const QVector<Channel> &channels) override;
    void refresh(int guideDays) override;
    void requestStream(const QString &channelId) override;
    void test() override;

    // Tests use a shorter time.
    void setWatchdogInterval(int ms)
    {
        m_watchdog.setInterval(ms);
    }

private:
    using Done = std::function<void(const QString &error)>;
    using Reply = std::function<void(const ApiResult &result)>;

    // Handshake, profile and login, unless there is a token already.
    void connectPortal(const Done &done);
    void handshake(int endpoint, const Done &done);
    void signIn(const Done &done);
    // A request with the token; a token the portal no longer accepts is renewed once.
    void get(const std::function<QUrl()> &url, const Reply &reply, bool retried = false);
    void setToken(const QString &token);

    TvhApi *m_api;
    QUrl m_root;
    QUrl m_endpoint; // the API address that answered the handshake
    Stalker::Device m_device;
    QString m_token;
    QHash<QString, QString> m_cmds; // channel ID → cmd for create_link
    QTimer m_watchdog;
    int m_generation = 0;
};

#endif // STALKERSOURCE_H
