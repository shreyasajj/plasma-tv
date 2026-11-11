// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "headers.h"

#include <QRegularExpression>

Headers HttpHeaders::parse(const QString &userAgent, const QString &text)
{
    Headers headers;

    if (!userAgent.trimmed().isEmpty()) {
        headers.append({QByteArrayLiteral("User-Agent"), userAgent.trimmed().toUtf8()});
    }

    static const QRegularExpression separator(QStringLiteral("[\\r\\n|]"));
    const QStringList entries = text.split(separator, Qt::SkipEmptyParts);

    for (const QString &entry : entries) {
        const qsizetype colon = entry.indexOf(QLatin1Char(':'));
        const QString name = entry.left(colon).trimmed();

        // Header names have no spaces; anything else is a typo, not a header.
        if (colon <= 0 || name.isEmpty() || name.contains(QLatin1Char(' '))) {
            continue;
        }

        headers.append({name.toLatin1(), entry.mid(colon + 1).trimmed().toUtf8()});
    }

    return headers;
}
