/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef XTREAMSOURCE_H
#define XTREAMSOURCE_H

#include "source.h"
#include "xtream.h"

class TvhApi;

// An Xtream Codes account: a username and password on a panel's player API.
// The password is part of every stream address, so streams are always relayed.
class XtreamSource : public Source
{
    Q_OBJECT

public:
    explicit XtreamSource(const SourceConfig &config, QObject *parent = nullptr);

    [[nodiscard]] QString identity() const override;

    void refresh(int guideDays) override;
    void requestStream(const QString &channelId) override;
    void test() override;
    void explainStreamError(const QString &channelId) override;

private:
    void fetchStreams(int generation);
    void fetchGuide(int generation, const Xmltv::ChannelMap &map);

    TvhApi *m_api;
    QUrl m_base;
    QString m_user;
    QString m_password;
    Xtream::Account m_account;
    // A User-Agent some panels want, on every request and the stream.
    Headers m_headers;
    int m_generation = 0;
    int m_guideDays = 3;
};

#endif // XTREAMSOURCE_H
