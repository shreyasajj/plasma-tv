/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QObject>
#include <QString>

#include <functional>

// Asynchronous key/value store for secrets. Only SourceSettings uses it, to
// move the password older versions kept in the system secret store (KWallet /
// Secret Service); tests use one in memory.
class SecretStore : public QObject
{
    Q_OBJECT

public:
    struct Result {
        bool ok = false;
        bool found = false;
        QString value;
        QString error;
    };
    using Callback = std::function<void(const Result &)>;

    using QObject::QObject;

    // A missing entry is ok, but not found.
    virtual void read(const QString &key, Callback done) = 0;
    // Removing a missing entry is ok.
    virtual void remove(const QString &key, Callback done) = 0;
};

// The system secret store through QtKeychain.
class KeychainStore : public SecretStore
{
    Q_OBJECT

public:
    using SecretStore::SecretStore;

    void read(const QString &key, Callback done) override;
    void remove(const QString &key, Callback done) override;
};

#endif // SECRETSTORE_H
