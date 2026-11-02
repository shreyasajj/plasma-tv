/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef STREAMRECORDER_H
#define STREAMRECORDER_H

#include <QFile>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <qqmlregistration.h>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

// Saves the live stream of a channel to "~/Videos/TV recordings" while playback is
// paused, so it can continue from the file. The file is deleted again by stop().
//
// Qt Multimedia stops at the length a file had when it was opened, so the recording is
// played from 'url' instead: a local HTTP stream of the file that follows it as it grows.
class StreamRecorder : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(QUrl url READ url NOTIFY recordingChanged)

public:
    explicit StreamRecorder(QObject *parent = nullptr);
    ~StreamRecorder() override;

    [[nodiscard]] bool recording() const
    {
        return m_reply != nullptr;
    }
    [[nodiscard]] QUrl url() const;

    // 'url' is what the player plays: a direct stream or the local relay, which keeps
    // credentials out of it.
    Q_INVOKABLE void start(const QUrl &url, const QString &channelName);
    // Stops recording and deletes the file.
    Q_INVOKABLE void stop();

Q_SIGNALS:
    void recordingChanged();
    void failed(const QString &message);

private:
    static QString directory();

    // Files of pauses that weren't cleaned up, e.g. after a crash.
    static void removeLeftovers();
    void serve();

    QNetworkAccessManager *m_networkManager;
    QPointer<QNetworkReply> m_reply;
    QFile m_file;
    QTcpServer *m_server;
};

#endif // STREAMRECORDER_H
