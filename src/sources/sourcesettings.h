/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef SOURCESETTINGS_H
#define SOURCESETTINGS_H

#include "sourceconfig.h"

#include <QList>
#include <QObject>
#include <QVariantList>
#include <qqmlregistration.h>

class QSettings;
class QQmlEngine;
class QJSEngine;
class SecretStore;

// The list of sources, in the order of the Settings page. Fields go to the
// config file ("sources/<n>/…"); the secrets of each source are one JSON
// object in the system secret store ("source/<id>/secret") and never reach
// QML in plain text.
//
// The single server of older versions ("server/…" and the "server-password"
// secret) becomes the first source on the first start.
class SourceSettings : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Sources)
    QML_SINGLETON

    // Each entry: id, type, name, enabled.
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(QStringList types READ types CONSTANT)
    Q_PROPERTY(bool configured READ configured NOTIFY changed)
    Q_PROPERTY(bool credentialsReady READ credentialsReady NOTIFY credentialsReadyChanged)
    Q_PROPERTY(QString secretStoreStatus READ secretStoreStatus NOTIFY secretStoreStatusChanged)

public:
    static SourceSettings *instance();
    static SourceSettings *create(QQmlEngine *, QJSEngine *);

    // Tests pass their own settings and secret store; 'store' becomes a child.
    SourceSettings(QSettings *settings, SecretStore *store, QObject *parent = nullptr);

    QList<SourceConfig> configs() const
    {
        return m_sources;
    }
    SourceConfig config(const QString &id) const;
    QVariantList sources() const;
    static QStringList types();
    // What the Settings page calls a type, e.g. "Xtream Codes".
    Q_INVOKABLE static QString typeName(const QString &type);
    // At least one enabled source.
    bool configured() const;
    bool credentialsReady() const
    {
        return m_credentialsReady;
    }
    QString secretStoreStatus() const
    {
        return m_secretStoreStatus;
    }

    // One source for its edit page: id, type, name, enabled, its fields and, for each
    // secret, "<key>Set" telling whether one is stored.
    Q_INVOKABLE QVariantMap source(const QString &id) const;
    // Values of the edit page ("id" empty for a new source; secrets only when edited).
    // Returns the source's ID.
    Q_INVOKABLE QString save(const QVariantMap &values);
    Q_INVOKABLE void remove(const QString &id);
    Q_INVOKABLE void move(const QString &id, int delta);
    // The stored source with the edit page's values applied, not saved (to test it).
    SourceConfig draft(const QVariantMap &values) const;

Q_SIGNALS:
    void changed();
    void credentialsReadyChanged();
    void secretStoreStatusChanged();
    // The settings of a source changed (or it was added).
    void saved(const QString &id);
    void removed(const QString &id);
    void reordered();

private:
    void load();
    bool migrate();
    void loadSecrets();
    void storeSecrets(const SourceConfig &config);
    void persist();
    void removeOldSettings();
    void pendingDone();
    void updateNetworkAuth();
    void setSecretStoreStatus(const QString &status);
    int indexOf(const QString &id) const;
    QString uniqueId() const;
    int freeIndex() const;

    QSettings *m_settings;
    SecretStore *m_store;
    QList<SourceConfig> m_sources;
    int m_pending = 0;
    bool m_credentialsReady = false;
    QString m_secretStoreStatus;
};

#endif // SOURCESETTINGS_H
