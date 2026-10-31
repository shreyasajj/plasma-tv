/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef HEADERS_H
#define HEADERS_H

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>

// Extra HTTP headers a source sends with every request, e.g. a User-Agent.
using Headers = QList<QPair<QByteArray, QByteArray>>;

namespace HttpHeaders
{
// Parses "Name: value" entries, separated by new lines or '|', plus an optional
// User-Agent of its own field. Entries without a name are skipped.
Headers parse(const QString &userAgent, const QString &text);
}

#endif // HEADERS_H
