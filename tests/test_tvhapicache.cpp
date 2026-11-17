// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "fakeserver.h"
#include "tvhapi.h"

#include <QTemporaryDir>
#include <QTest>

// Counts actual HTTP requests to ensure guide caching coalesces work and survives API restarts.
class TvhApiCacheTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void reusesDownloadsAcrossRestartsAndTokenRenewal()
    {
        QTemporaryDir directory;
        FakeServer server([](const FakeServer::Request &) {
            return FakeServer::Reply{.body = "guide"};
        });
        const QUrl url(server.address() + QStringLiteral("guide"));

        {
            TvhApi api(nullptr, directory.path());
            api.setHeaders(
                {{"Cookie", "timezone=America/Chicago"}, {"Authorization", "Bearer one"}},
                true);
            int callbacks = 0;
            const auto done = [&callbacks](const ApiResult &result) {
                if (result.ok && result.body == "guide") {
                    ++callbacks;
                }
            };
            api.getGuide(url, done);
            api.getGuide(url, done);
            QTRY_COMPARE(callbacks, 2);
            QCOMPARE(server.requests.size(), 1);
        }

        TvhApi restarted(nullptr, directory.path());
        restarted.setHeaders(
            {{"Cookie", "timezone=America/Chicago"}, {"Authorization", "Bearer two"}},
            true);
        bool loaded = false;
        restarted.getGuide(url, [&loaded](const ApiResult &result) {
            loaded = result.ok;
        });
        QTRY_VERIFY(loaded);
        QCOMPARE(server.requests.size(), 1);
        restarted.setHeaders({{"Cookie", "timezone=Asia/Kolkata"}});
        loaded = false;
        restarted.getGuide(url, [&loaded](const ApiResult &result) {
            loaded = result.ok;
        });
        QTRY_VERIFY(loaded);
        QCOMPARE(server.requests.size(), 2);
    }

    void customAuthorizationKeepsAccountsSeparate()
    {
        QTemporaryDir directory;
        FakeServer server([](const FakeServer::Request &) {
            return FakeServer::Reply{.body = "guide"};
        });
        TvhApi api(nullptr, directory.path());
        const QUrl url(server.address());
        int completed = 0;
        const auto done = [&completed](const ApiResult &result) {
            if (result.ok) {
                ++completed;
            }
        };
        api.setHeaders({{"Authorization", "Bearer account-one"}});
        api.getGuide(url, done);
        QTRY_COMPARE(completed, 1);
        api.setHeaders({{"Authorization", "Bearer account-two"}});
        api.getGuide(url, done);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(server.requests.size(), 2);
    }

    void retriesFailedDownloadsAndCachesSuccessfulEmptyReplies()
    {
        QTemporaryDir directory;
        int calls = 0;
        FakeServer server([&calls](const FakeServer::Request &) {
            return ++calls == 1 ? FakeServer::Reply{.status = 500} : FakeServer::Reply{.body = {}};
        });
        TvhApi api(nullptr, directory.path());
        const QUrl url(server.address());
        int completed = 0;
        api.getGuide(url, [&completed](const ApiResult &result) {
            if (!result.ok) {
                ++completed;
            }
        });
        QTRY_COMPARE(completed, 1);
        const auto done = [&completed](const ApiResult &result) {
            if (result.ok && result.body.isEmpty()) {
                ++completed;
            }
        };
        api.getGuide(url, done);
        QTRY_COMPARE(completed, 2);
        api.getGuide(url, done);
        QTRY_COMPARE(completed, 3);
        QCOMPARE(server.requests.size(), 2);
    }

    void abortingACacheLookupDoesNotFetchOrCallBack()
    {
        QTemporaryDir directory;
        FakeServer server([](const FakeServer::Request &) {
            return FakeServer::Reply{.body = "guide"};
        });
        TvhApi api(nullptr, directory.path());
        const QUrl url(server.address());
        int aborted = 0;
        int completed = 0;
        api.getGuide(url, [&aborted](const ApiResult &) {
            ++aborted;
        });
        api.abortAll();
        api.getGuide(url, [&completed](const ApiResult &result) {
            if (result.ok) {
                ++completed;
            }
        });
        QTRY_COMPARE(completed, 1);
        QCOMPARE(aborted, 0);
        QCOMPARE(server.requests.size(), 1);
    }

    void postGuideUsesAStablePageKey()
    {
        QTemporaryDir directory;
        FakeServer server([](const FakeServer::Request &) {
            return FakeServer::Reply{.body = "page"};
        });
        ServerConfig config;
        config.host = QStringLiteral("127.0.0.1");
        config.port = QUrl(server.address()).port();
        TvhApi api(nullptr, directory.path());
        api.setServer(config);
        int completed = 0;
        const auto done = [&completed](const ApiResult &result) {
            if (result.ok) {
                ++completed;
            }
        };
        api.postGuide(QStringLiteral("guide"),
                      QUrlQuery(QStringLiteral("now=1000")),
                      QStringLiteral("page0/days3"),
                      done);
        QTRY_COMPARE(completed, 1);
        api.postGuide(QStringLiteral("guide"),
                      QUrlQuery(QStringLiteral("now=2000")),
                      QStringLiteral("page0/days3"),
                      done);
        QTRY_COMPARE(completed, 2);
        QCOMPARE(server.requests.size(), 1);
        api.postGuide(QStringLiteral("guide"), {}, QStringLiteral("page1/days3"), done);
        QTRY_COMPARE(completed, 3);
        QCOMPARE(server.requests.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TvhApiCacheTest)

#include "test_tvhapicache.moc"
