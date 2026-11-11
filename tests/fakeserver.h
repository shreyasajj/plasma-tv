/*
 * SPDX-FileCopyrightText: 2026 shreyasajj
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef FAKESERVER_H
#define FAKESERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

// A local HTTP server for tests: answers each request with what the handler returns.
class FakeServer : public QObject
{
public:
    struct Request {
        QByteArray method;
        QUrl url; // path and query
        QByteArray head; // request line and headers

        QString query(const QString &key) const
        {
            return QUrlQuery(url).queryItemValue(key, QUrl::FullyDecoded);
        }

        bool hasHeader(const QByteArray &line) const
        {
            return head.contains(line);
        }
    };
    struct Reply {
        int status = 200;
        QByteArray body;
        QByteArray type = "application/json";
    };
    using Handler = std::function<Reply(const Request &)>;

    explicit FakeServer(Handler handler)
        : m_handler(std::move(handler))
    {
        if (!m_server.listen(QHostAddress::LocalHost)) {
            qFatal("Cannot listen on the loopback address");
        }

        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                auto *buffer = new QByteArray;
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer]() {
                    buffer->append(socket->readAll());
                    const qsizetype end = buffer->indexOf("\r\n\r\n");

                    if (end < 0) {
                        return;
                    }

                    Request request;
                    request.head = buffer->left(end);
                    const QList<QByteArray> line =
                        request.head.left(request.head.indexOf("\r\n")).split(' ');
                    request.method = line.value(0);
                    request.url = QUrl::fromEncoded(line.value(1));
                    buffer->clear();
                    requests.append(request);

                    const Reply reply = m_handler(request);
                    socket->write("HTTP/1.1 " + QByteArray::number(reply.status)
                                  + " X\r\nContent-Type: " + reply.type
                                  + "\r\nContent-Length: " + QByteArray::number(reply.body.size())
                                  + "\r\nConnection: close\r\n\r\n" + reply.body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, [socket, buffer]() {
                    delete buffer;
                    socket->deleteLater();
                });
            }
        });
    }

    QString address() const
    {
        return QStringLiteral("http://127.0.0.1:%1/").arg(m_server.serverPort());
    }

    QList<Request> requests;

private:
    QTcpServer m_server;
    Handler m_handler;
};

#endif // FAKESERVER_H
