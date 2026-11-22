// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "networkauth.h"

#include <QAuthenticator>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace
{
QMutex s_mutex;
QList<NetworkAuth::HostCredentials> s_credentials;

constexpr const char *s_attempts = "tvhAuthAttempts";
constexpr const char *s_user = "tvhUser";
constexpr const char *s_password = "tvhPassword";
}

void NetworkAuth::setCredentials(const QList<HostCredentials> &credentials)
{
    QMutexLocker const lock(&s_mutex);
    s_credentials = credentials;
}

QList<NetworkAuth::HostCredentials> NetworkAuth::credentials()
{
    QMutexLocker const lock(&s_mutex);

    return s_credentials;
}

void NetworkAuth::setReplyCredentials(QNetworkReply *reply,
                                      const QString &user,
                                      const QString &password)
{
    reply->setProperty(s_user, user);
    reply->setProperty(s_password, password);
}

void NetworkAuth::install(const QNetworkAccessManager *networkManager)
{
    QObject::connect(networkManager,
                     &QNetworkAccessManager::authenticationRequired,
                     networkManager,
                     [](QNetworkReply *reply, QAuthenticator *auth) {
                         const int attempts = reply->property(s_attempts).toInt();
                         if (attempts > 0) {
                             return; // give up, the reply finishes with an authentication error
                         }

                         reply->setProperty(s_attempts, attempts + 1);

                         QString user;
                         QString password;
                         if (reply->property(s_user).isValid()) {
                             user = reply->property(s_user).toString();
                             password = reply->property(s_password).toString();
                         } else {
                             // Never leak credentials to a host other than their source's.
                             const QString host = reply->url().host();

                             for (const HostCredentials &entry : credentials()) {
                                 if (entry.host == host) {
                                     user = entry.username;
                                     password = entry.password;
                                     break;
                                 }
                             }
                         }

                         if (user.isEmpty()) {
                             return;
                         }

                         auth->setUser(user);
                         auth->setPassword(password);
                     });
}
