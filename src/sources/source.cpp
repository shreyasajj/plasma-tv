// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "source.h"

#include "m3usource.h"
#include "tvheadendsource.h"

Source *Source::create(const SourceConfig &config, QObject *parent)
{
    if (config.type == SourceConfig::Tvheadend) {
        return new TvheadendSource(config, parent);
    }

    if (config.type == SourceConfig::M3u) {
        return new M3uSource(config, parent);
    }

    return nullptr;
}

Source::Source(const SourceConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
}

void Source::restoreChannels(const QVector<Channel> &)
{
}

void Source::record(const EpgEvent &)
{
}

void Source::cancelRecording(const EpgEvent &)
{
}
