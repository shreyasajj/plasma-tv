// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "source.h"

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

void Source::explainStreamError(const QString &)
{
}
