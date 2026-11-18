/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef EPGSTORE_H
#define EPGSTORE_H

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QVariantMap>
#include <QVector>

struct Channel {
    QString uuid;
    int number = 0;
    QString name;
    QString icon; // as reported by the server, may be relative ("imagecache/12")
    QString stream; // M3U sources only; TVHeadend streams are built from the uuid

    QString controlId; // Stable automation ID; empty for ambiguous M3U entries.

    static Channel fromJson(const QJsonObject &object);
};

struct EpgEvent {
    qint64 id = 0;
    QString channel;
    qint64 start = 0; // seconds since epoch
    qint64 stop = 0;
    QString title;
    QString subtitle;
    QString summary;
    QString description;
    uint8_t genre = 0; // ETSI EN 300 468 content nibble, e.g. 0x10 = movie
    QString dvrUuid;
    QString dvrState;

    static EpgEvent fromJson(const QJsonObject &object);
    [[nodiscard]] QVariantMap toVariant() const;
};

// In-memory index of EPG events per channel, sorted by start time.
class EpgStore
{
public:
    void setEvents(QVector<EpgEvent> events);
    void clear();

    [[nodiscard]] bool isEmpty() const
    {
        return m_byChannel.isEmpty();
    }

    [[nodiscard]] size_t size() const
    {
        return m_size;
    }

    [[nodiscard]] const EpgEvent *eventAt(const QString &channel, qint64 time) const;
    [[nodiscard]] const EpgEvent *nextAfter(const QString &channel, qint64 time) const;
    [[nodiscard]] const EpgEvent *byId(qint64 id) const;

    EpgEvent *byId(qint64 id);

    [[nodiscard]] QVector<const EpgEvent *>
    range(const QString &channel, qint64 from, qint64 to) const;

    // Latest stop time of any event, used to bound guide navigation.
    [[nodiscard]] qint64 lastStop() const
    {
        return m_lastStop;
    }

private:
    QHash<QString, QVector<EpgEvent>> m_byChannel;
    QHash<qint64, QPair<QString, int>> m_index;
    qint64 m_lastStop = 0;
    size_t m_size = 0;
};

QString genreName(uint8_t genre);

#endif // EPGSTORE_H
