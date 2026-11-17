/*
 * SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
 *
 * SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
 */

#ifndef TVHAPI_H
#define TVHAPI_H

#include "guidecache.h"
#include "serverconfig.h"
#include "sources/headers.h"

#include <QHash>
#include <QJsonDocument>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QUrlQuery>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

struct ApiResult {
    bool ok = false;
    int httpStatus = 0;
    QByteArray body;
    QString error;

    [[nodiscard]] QJsonDocument json() const
    {
        return QJsonDocument::fromJson(body);
    }
};

// Thin asynchronous wrapper around the TVHeadend HTTP API; also fetches M3U
// playlists and XMLTV guides with the same credentials handling. Guide responses
// persist for a day; cache reads and writes run on a dedicated worker thread.
class TvhApi : public QObject
{
    Q_OBJECT

public:
    using Callback = std::function<void(const ApiResult &)>;

    explicit TvhApi(QObject *parent = nullptr, const QString &cacheDirectory = {});
    ~TvhApi() override;

    void setServer(const ServerConfig &config);
    // Sent with every request, e.g. a User-Agent a provider wants. A session bearer token
    // may change without changing the account or invalidating its guide cache.
    void setHeaders(const Headers &headers, bool sessionAuthorization = false)
    {
        m_headers = headers;
        m_sessionAuthorization = sessionAuthorization;
    }

    [[nodiscard]] const ServerConfig &server() const
    {
        return m_server;
    }

    void get(const QString &path, const QUrlQuery &query, Callback callback);
    void get(const QUrl &url, Callback callback);
    void post(const QString &path, const QUrlQuery &form, Callback callback);

    // Guide downloads are reused for 24 hours; account, URL and source headers identify them.
    void getGuide(const QUrl &url, Callback callback);
    // 'cacheTag' identifies a page/window without including its changing current-time filter.
    void postGuide(const QString &path,
                   const QUrlQuery &form,
                   const QString &cacheTag,
                   Callback callback);
    static void clearGuideCache();

    // One-shot request against an arbitrary configuration (used by "Test connection").
    void probe(const ServerConfig &config, const QString &path, Callback callback);
    void probe(const ServerConfig &config, const QUrl &url, Callback callback);

    // Aborts all pending requests without invoking their callbacks.
    void abortAll();

private:
    void track(QNetworkReply *reply, Callback callback);
    [[nodiscard]] QByteArray guideKey(const QUrl &url, const QByteArray &request) const;
    void
    cachedGuide(const QByteArray &key, Callback callback, std::function<void(Callback)> request);
    void useGuide(const QByteArray &key,
                  int generation,
                  const std::optional<QByteArray> &body,
                  const std::function<void(Callback)> &request);
    void saveGuide(const QByteArray &key, int generation, const ApiResult &result);
    void finishGuide(const QByteArray &key, int generation, const ApiResult &result);

    GuideCache m_guideCache;
    QThread m_cacheThread;
    QObject *m_cacheWorker;
    int m_cacheGeneration = 0;
    QHash<QByteArray, QList<Callback>> m_guidePending;

    QNetworkAccessManager *m_networkManager;
    ServerConfig m_server;
    Headers m_headers;
    bool m_sessionAuthorization = false;
    QSet<QNetworkReply *> m_pending;
};

#endif // TVHAPI_H
