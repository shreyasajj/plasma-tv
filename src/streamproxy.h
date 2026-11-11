/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STREAMPROXY_H
#define STREAMPROXY_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QUrl>

class QNetworkAccessManager;
class QTcpServer;
class QTcpSocket;

// Relays streams through 127.0.0.1. The media player gets an address without
// credentials (FFmpeg would print them to the log), playback runs a few seconds
// behind live (see StreamBuffer), and when the server connection breaks the
// relay reconnects by itself while the player keeps playing from the buffer.
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

    explicit StreamProxy(QObject *parent = nullptr);

    using Headers = QList<QPair<QByteArray, QByteArray>>;

    struct Target {
        QUrl url;
        // Answered to the server's challenges.
        QString username;
        QString password;
        // A StreamBuffer mode.
        int bufferMode = 0;
        // Sent with every request, reconnects included (e.g. a User-Agent).
        Headers headers;
        // The address itself holds a secret (Xtream puts the password in its path), so the
        // player is never sent there, not even for HLS.
        bool secretUrl = false;
    };

    // Returns a local address for 'target'. Credentials in its URL are used instead of
    // 'username' and 'password' and removed from it. An empty URL means the relay could not be
    // started.
    QUrl relay(Target target);
    QUrl
    relay(const QUrl &upstream, const QString &username, const QString &password, int bufferMode);

    // HLS playlists are fetched by the player itself: relative segment addresses
    // wouldn't resolve against the relay.
    static bool isHls(const QUrl &url);

    // Tests use shorter times.
    void setTiming(const Timing &timing)
    {
        m_timing = timing;
    }

private:
    bool listen();
    void handleConnection(QTcpSocket *socket);
    void serve(QTcpSocket *socket, const QByteArray &request);

    QTcpServer *m_server;
    QNetworkAccessManager *m_networkManager;
    QHash<QString, Target> m_targets;
    QStringList m_order; // oldest token first
    Timing m_timing;
};

#endif // STREAMPROXY_H
