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

    // Returns a local address for 'upstream'. The credentials are answered to the server's
    // challenges; ones in the URL are used instead and removed from it. 'bufferMode' is a
    // StreamBuffer mode. An empty URL means the relay could not be started.
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

    struct Target {
        QUrl url;
        QString username;
        QString password;
        int bufferMode = 0;
    };

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
