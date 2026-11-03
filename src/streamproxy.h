/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STREAMPROXY_H
#define STREAMPROXY_H

#include "serverconfig.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class StreamSession;
class QTcpServer;
class QTcpSocket;

// Relays streams through 127.0.0.1. The media player gets an address without
// credentials (FFmpeg would print them to the log), playback runs a few seconds
// behind live (see StreamBuffer), and when the server connection breaks the
// relay reconnects by itself while the player keeps playing from the buffer.
//
// With rewinding, a session also writes the stream to disk (see TimeshiftBuffer) and
// lives on when the player disconnects, so the player can come back at an earlier
// moment through behind().
class StreamProxy : public QObject
{
    Q_OBJECT

public:
    struct Timing {
        // No data from the server for this long counts as a broken connection.
        int stallMs = 4000;
        // Pause before each reconnect; after the last one the player's connection is closed.
        QList<int> retryDelaysMs = {500, 1000, 2000, 2000, 2000};
    };

    struct TimeshiftOptions {
        // Each session writes to a folder in here.
        QString root;
        qint64 minFreeBytes = 0;
        qint64 chunkMs = 60000;
        // Free bytes on the disk; tests pass their own.
        std::function<qint64(const QString &directory)> freeBytes;
    };

    struct TimeshiftState {
        // The format is known: MPEG-TS or not.
        bool known = false;
        // Something is written that can be played from.
        bool available = false;
        // Writing stopped because the disk is nearly full.
        bool stopped = false;
        qint64 windowMs = 0;
    };

    explicit StreamProxy(QObject *parent = nullptr);
    ~StreamProxy() override;

    using Headers = QList<QPair<QByteArray, QByteArray>>;

    // Returns a local address for 'upstream' that is fetched with the extra 'headers' (e.g. a
    // User-Agent). The credentials of 'server' are answered to the server's challenges; ones in
    // the URL are used instead and removed from it. 'bufferMode' is a StreamBuffer mode. An
    // empty URL means the relay could not be started.
    QUrl
    relay(const QUrl &upstream, const ServerConfig &server, const Headers &headers, int bufferMode);

    // Keeps the last 'maxMs' of the stream behind 'relayUrl' on disk, from when the player
    // first connects. Has to be called before that; 0 turns it off.
    void setRewind(const QUrl &relayUrl, qint64 maxMs);
    // Starts keeping everything from now on, for a pause. False when the stream isn't
    // MPEG-TS or isn't playing.
    bool hold(const QUrl &relayUrl);
    // Ends the session of 'relayUrl': closes its connections and deletes its files.
    void end(const QUrl &relayUrl);
    [[nodiscard]] TimeshiftState timeshift(const QUrl &relayUrl) const;
    // The address that plays 'relayUrl' 'behindMs' behind live.
    static QUrl behind(const QUrl &relayUrl, qint64 behindMs);

    void setTimeshiftOptions(const TimeshiftOptions &options)
    {
        m_timeshiftOptions = options;
    }

    // HLS playlists are fetched by the player itself: relative segment addresses
    // wouldn't resolve against the relay.
    static bool isHls(const QUrl &url);

    // Tests use shorter times.
    void setTiming(const Timing &timing)
    {
        m_timing = timing;
    }

    struct Target {
        QUrl url;
        QString username;
        QString password;
        Headers headers;
        int bufferMode = 0;
        qint64 rewindMs = 0;
    };

Q_SIGNALS:
    // Rewinding became available or stopped for 'relayUrl'.
    void timeshiftChanged(const QUrl &relayUrl);

private:
    bool listen();
    void handleConnection(QTcpSocket *socket);
    void serve(QTcpSocket *socket, const QByteArray &request);
    [[nodiscard]] QUrl address(const QString &token) const;
    [[nodiscard]] static QString tokenOf(const QUrl &relayUrl);
    [[nodiscard]] StreamSession *session(const QString &token) const;

    QTcpServer *m_server;
    QNetworkAccessManager *m_networkManager;
    QHash<QString, Target> m_targets;
    QStringList m_order; // oldest token first
    QHash<QString, QPointer<StreamSession>> m_sessions;
    Timing m_timing;
    TimeshiftOptions m_timeshiftOptions;
};

#endif // STREAMPROXY_H
