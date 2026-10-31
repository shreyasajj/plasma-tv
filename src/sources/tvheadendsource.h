/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TVHEADENDSOURCE_H
#define TVHEADENDSOURCE_H

#include "source.h"

class TvhApi;

// A TVHeadend server: channels and the guide through its JSON API, streams with a
// short-lived ticket, and recordings on the server.
class TvheadendSource : public Source
{
    Q_OBJECT

public:
    explicit TvheadendSource(const SourceConfig &config, QObject *parent = nullptr);

    Capabilities capabilities() const override
    {
        return {.record = true};
    }
    QString identity() const override;

    void refresh(int guideDays) override;
    void requestStream(const QString &channelId) override;
    void test() override;
    void record(const EpgEvent &event) override;
    void cancelRecording(const EpgEvent &event) override;

private:
    void fetchChannels(int generation);
    void fetchEpgPage(int generation, int offset);
    QUrl streamUrl(const QString &channelId) const;

    TvhApi *m_api;
    ServerConfig m_server;
    int m_generation = 0;
    int m_guideDays = 3;
    QVector<EpgEvent> m_incoming;
};

#endif // TVHEADENDSOURCE_H
