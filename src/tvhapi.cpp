// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "tvhapi.h"

#include "networkauth.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>

namespace
{
QString guideDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/guide-responses");
}

// Do not leave a guide or API request waiting indefinitely on a stalled server.
constexpr int s_timeoutMs = 20000;

QNetworkRequest makeRequest(const QUrl &url, const Headers &headers)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(s_timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("flimmer/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    for (const auto &[name, value] : headers) {
        request.setRawHeader(name, value);
    }

    return request;
}

QString describeError(const QNetworkReply *reply, int status)
{
    if (status == 401 || reply->error() == QNetworkReply::AuthenticationRequiredError) {
        return QObject::tr("Authentication failed – check username and password");
    }

    if (status == 403) {
        return QObject::tr("Access denied – the user lacks permission on the server");
    }

    if (status == 404) {
        return QObject::tr("Not found (%1)").arg(reply->url().path());
    }

    if (reply->error() == QNetworkReply::OperationCanceledError) {
        return QObject::tr("The server did not respond in time");
    }

    if (reply->error() == QNetworkReply::HostNotFoundError) {
        return QObject::tr("Server address not found");
    }

    if (reply->error() == QNetworkReply::ConnectionRefusedError) {
        return QObject::tr("Connection refused – is the server running on that port?");
    }

    // Qt's own text for these holds the whole address, and with it the password some sources
    // put there.
    if (status >= 400) {
        return QObject::tr("The server answered with an error (%1)").arg(status);
    }

    return reply->errorString();
}

// A guide or playlist may live on another host than the server; it never gets the credentials.
void setCredentials(QNetworkReply *reply, const ServerConfig &config)
{
    if (reply->url().host() == config.baseUrl().host()) {
        NetworkAuth::setReplyCredentials(reply, config.username, config.password);
    } else {
        NetworkAuth::setReplyCredentials(reply, {}, {});
    }
}
}

TvhApi::TvhApi(QObject *parent, const QString &cacheDirectory)
    : QObject(parent)
    , m_guideCache(cacheDirectory.isEmpty() ? guideDirectory() : cacheDirectory)
    , m_cacheWorker(new QObject)
    , m_networkManager(new QNetworkAccessManager(this))
{
    NetworkAuth::install(m_networkManager);
    m_cacheWorker->moveToThread(&m_cacheThread);
    connect(&m_cacheThread, &QThread::finished, m_cacheWorker, &QObject::deleteLater);
    m_cacheThread.start();
}

TvhApi::~TvhApi()
{
    m_cacheThread.quit();
    m_cacheThread.wait();
}

void TvhApi::setServer(const ServerConfig &config)
{
    if (config == m_server) {
        return;
    }

    abortAll();
    m_server = config;
    m_networkManager->clearAccessCache();
}

void TvhApi::get(const QString &path, const QUrlQuery &query, Callback callback)
{
    QUrl url = m_server.url(path);
    url.setQuery(query);
    get(url, std::move(callback));
}

void TvhApi::get(const QUrl &url, Callback callback)
{
    QNetworkReply *reply = m_networkManager->get(makeRequest(url, m_headers));
    setCredentials(reply, m_server);
    track(reply, std::move(callback));
}

void TvhApi::post(const QString &path, const QUrlQuery &form, Callback callback)
{
    QNetworkRequest request = makeRequest(m_server.url(path), m_headers);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));

    // QUrlQuery leaves '+' alone, which form decoding would turn into a space.
    const QByteArray body = form.toString(QUrl::FullyEncoded).toUtf8().replace('+', "%2B");
    QNetworkReply *reply = m_networkManager->post(request, body);
    NetworkAuth::setReplyCredentials(reply, m_server.username, m_server.password);
    track(reply, std::move(callback));
}

QByteArray TvhApi::guideKey(const QUrl &url, const QByteArray &request) const
{
    QByteArray key;
    QDataStream stream(&key, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_5);
    stream << url.toEncoded() << request << m_server.username << m_server.password;

    for (const auto &[name, value] : m_headers) {
        // A renewed bearer token must not turn an existing guide into a cache miss.
        if (!m_sessionAuthorization || name.compare("Authorization", Qt::CaseInsensitive) != 0) {
            stream << name << value;
        }
    }

    return QCryptographicHash::hash(key, QCryptographicHash::Sha256);
}

void TvhApi::getGuide(const QUrl &url, Callback callback)
{
    cachedGuide(guideKey(url, "GET"), std::move(callback), [this, url](Callback done) {
        get(url, std::move(done));
    });
}

void TvhApi::postGuide(const QString &path,
                       const QUrlQuery &form,
                       const QString &cacheTag,
                       Callback callback)
{
    cachedGuide(guideKey(m_server.url(path), "POST " + cacheTag.toUtf8()),
                std::move(callback),
                [this, path, form](Callback done) {
                    post(path, form, std::move(done));
                });
}

void TvhApi::cachedGuide(const QByteArray &key,
                         Callback callback,
                         std::function<void(Callback)> request)
{
    auto &pending = m_guidePending[key];
    pending.append(std::move(callback));

    if (pending.size() > 1) {
        return;
    }

    const int generation = m_cacheGeneration;
    QMetaObject::invokeMethod(
        m_cacheWorker,
        [this, key, generation, request = std::move(request)]() {
            const auto body = m_guideCache.read(key);
            QMetaObject::invokeMethod(
                this,
                [this, key, generation, body, request]() {
                    useGuide(key, generation, body, request);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void TvhApi::useGuide(const QByteArray &key,
                      int generation,
                      const std::optional<QByteArray> &body,
                      const std::function<void(Callback)> &request)
{
    if (generation != m_cacheGeneration) {
        return;
    }

    if (body) {
        finishGuide(key, generation, {.ok = true, .httpStatus = 200, .body = *body});

        return;
    }

    request([this, key, generation](const ApiResult &result) {
        saveGuide(key, generation, result);
    });
}

void TvhApi::saveGuide(const QByteArray &key, int generation, const ApiResult &result)
{
    // Empty successful replies are cached too: some portals disable their bulk-guide endpoint.
    if (!result.ok || result.body.contains("Authorization failed")) {
        finishGuide(key, generation, result);

        return;
    }

    QMetaObject::invokeMethod(
        m_cacheWorker,
        [this, key, generation, result]() {
            m_guideCache.write(key, result.body);
            QMetaObject::invokeMethod(
                this,
                [this, key, generation, result]() {
                    finishGuide(key, generation, result);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void TvhApi::finishGuide(const QByteArray &key, int generation, const ApiResult &result)
{
    if (generation != m_cacheGeneration) {
        return;
    }

    const auto callbacks = m_guidePending.take(key);

    for (const Callback &callback : callbacks) {
        callback(result);
    }
}

void TvhApi::clearGuideCache()
{
    GuideCache(guideDirectory()).clear();
}

void TvhApi::probe(const ServerConfig &config, const QString &path, Callback callback)
{
    probe(config, config.url(path), std::move(callback));
}

void TvhApi::probe(const ServerConfig &config, const QUrl &url, Callback callback)
{
    // A fresh manager so cached credentials/connections of the active server don't interfere.
    auto *networkManager = new QNetworkAccessManager(this);
    NetworkAuth::install(networkManager);
    QNetworkReply *reply = networkManager->get(makeRequest(url, m_headers));
    setCredentials(reply, config);

    connect(reply,
            &QNetworkReply::finished,
            this,
            [reply, networkManager, callback = std::move(callback)] {
                ApiResult result;
                result.httpStatus =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                result.body = reply->readAll();
                result.ok = reply->error() == QNetworkReply::NoError;

                if (!result.ok) {
                    result.error = describeError(reply, result.httpStatus);
                }

                reply->deleteLater();
                networkManager->deleteLater();
                callback(result);
            });
}

void TvhApi::abortAll()
{
    ++m_cacheGeneration;
    m_guidePending.clear();
    const auto pending = m_pending;
    m_pending.clear();

    for (QNetworkReply *reply : pending) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void TvhApi::track(QNetworkReply *reply, Callback callback)
{
    m_pending.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)] {
        m_pending.remove(reply);
        ApiResult result;
        result.httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.body = reply->readAll();
        result.ok = reply->error() == QNetworkReply::NoError;

        if (!result.ok) {
            result.error = describeError(reply, result.httpStatus);
        }

        reply->deleteLater();
        callback(result);
    });
}
