/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef NETWORKAUTH_H
#define NETWORKAUTH_H

#include <QList>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace NetworkAuth
{
struct HostCredentials {
    QString host;
    QString username;
    QString password;
};

// Thread-safe list of the sources' credentials, one entry per host. Used by
// network access managers living on other threads (e.g. the QML image loader).
void setCredentials(const QList<HostCredentials> &credentials);
QList<HostCredentials> credentials();

// Answers HTTP Basic/Digest challenges from TVHeadend. Credentials can be
// overridden per reply with setReplyCredentials(); otherwise those of the
// reply's host are used. Each reply gets exactly one attempt so wrong
// credentials fail fast instead of looping.
void install(const QNetworkAccessManager *networkManager);
void setReplyCredentials(QNetworkReply *reply, const QString &user, const QString &password);
}

#endif // NETWORKAUTH_H
