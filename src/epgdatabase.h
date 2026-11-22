/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef EPGDATABASE_H
#define EPGDATABASE_H

#include "epgstore.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QThread>
#include <QVector>

#include <functional>

// SQLite cache of channels and EPG data so the UI is populated instantly on
// start-up and stays usable while the server is unreachable.
//
// All database work runs in order on a thread of its own: saving a guide of
// several days takes long enough to make playback and the remote stutter.
class EpgDatabase
{
public:
    EpgDatabase();
    ~EpgDatabase();

    bool open();
    // Identifies what a source's cached channels and guide came from (Source::identity()).
    [[nodiscard]] QString sourceIdentity(const QString &sourceId) const
    {
        return m_identities.value(sourceId);
    }

    void setSourceIdentity(const QString &sourceId, const QString &identity);
    [[nodiscard]] QStringList sources() const
    {
        return m_identities.keys();
    }

    [[nodiscard]] qint64 lastUpdate() const;

    void clear();
    // Forgets the channels and guide of one source; IDs of its channels start with "<id>:".
    void clearSource(const QString &sourceId);
    // Replaces the channels and the guide of one source.
    void saveChannels(const QString &sourceId, const QVector<Channel> &channels);
    [[nodiscard]] QVector<Channel> loadChannels() const;
    void saveEvents(const QString &sourceId, const QVector<EpgEvent> &events, qint64 updatedAt);
    // Calls 'done' on the thread of 'context' once the events are loaded. 'context' must
    // outlive the database.
    void loadEvents(qint64 notEndedBefore,
                    QObject *context,
                    std::function<void(QVector<EpgEvent>)> const &done) const;

    void updateRecording(qint64 eventId, const QString &dvrUuid, const QString &dvrState) const;

private:
    // Runs 'job' on the database thread after everything queued before.
    void run(std::function<void()> job) const;

    // Like run(), but waits for 'job' to finish.
    void runAndWait(std::function<void()> job) const;

    [[nodiscard]] QString meta(const QString &key) const;
    void setMeta(const QString &key, const QString &value) const;

    QString m_connectionName;
    QHash<QString, QString> m_identities;
    QThread m_thread;

    // Lives on m_thread; the jobs are queued to it.
    QObject *m_worker;
};

#endif // EPGDATABASE_H
