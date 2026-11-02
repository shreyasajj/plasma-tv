/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef M3USOURCE_H
#define M3USOURCE_H

#include "source.h"
#include "xmltv.h"

#include <QHash>

class TvhApi;

// An M3U playlist (e.g. from Dispatcharr) with an XMLTV guide.
class M3uSource : public Source
{
    Q_OBJECT

public:
    explicit M3uSource(const SourceConfig &config, QObject *parent = nullptr);

    [[nodiscard]] QString identity() const override;

    void restoreChannels(const QVector<Channel> &channels) override;
    void refresh(int guideDays) override;
    void requestStream(const QString &channelId) override;
    void test() override;

private:
    void fetchGuide(int generation, const QUrl &url, const Xmltv::ChannelMap &map);

    TvhApi *m_api;
    ServerConfig m_server;
    int m_generation = 0;
    int m_guideDays = 3;
    QHash<QString, QString> m_streams; // channel ID → stream address
    // User-Agent and extra headers some providers want, on every request and the stream.
    Headers m_headers;
};

#endif // M3USOURCE_H
