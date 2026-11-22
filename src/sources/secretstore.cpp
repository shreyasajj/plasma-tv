// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "secretstore.h"

#include <qt6keychain/keychain.h>

namespace
{
// Service name of the entries in the system secret store.
constexpr QLatin1StringView s_service{"flimmer"};

void run(QKeychain::Job *job, const QString &key, SecretStore::Callback done)
{
    job->setKey(key);
    QObject::connect(job, &QKeychain::Job::finished, job, [job, done = std::move(done)]() {
        SecretStore::Result result;
        result.ok = job->error() == QKeychain::NoError || job->error() == QKeychain::EntryNotFound;
        result.found = job->error() == QKeychain::NoError;

        if (auto *read = qobject_cast<QKeychain::ReadPasswordJob *>(job); read && result.found) {
            result.value = read->textData();
        }

        if (!result.ok) {
            result.error = job->errorString();
        }

        done(result);
    });
    job->start();
}
}

void KeychainStore::read(const QString &key, Callback done)
{
    run(new QKeychain::ReadPasswordJob(s_service, this), key, std::move(done));
}

void KeychainStore::remove(const QString &key, Callback done)
{
    run(new QKeychain::DeletePasswordJob(s_service, this), key, std::move(done));
}
