// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "source.h"

#include "m3usource.h"
#include "tvheadendsource.h"
#include "xtreamsource.h"

// Kept apart from source.cpp, so tests of one source don't need all the others.
Source *Source::create(const SourceConfig &config, QObject *parent)
{
    if (config.type == SourceConfig::Tvheadend) {
        return new TvheadendSource(config, parent);
    }

    if (config.type == SourceConfig::M3u) {
        return new M3uSource(config, parent);
    }

    if (config.type == SourceConfig::Xtream) {
        return new XtreamSource(config, parent);
    }

    return nullptr;
}
