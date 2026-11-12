// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "channelmodel.h"
#include "fakeserver.h"
#include "sources/secretstore.h"
#include "sources/sourcesettings.h"
#include "tvhbackend.h"

#include <QFile>
#include <QGuiApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace
{
// Nothing is stored in the secret store any more; it is only read to move an old password.
class EmptyStore : public SecretStore
{
public:
    void read(const QString &, Callback done) override
    {
        done({.ok = true});
    }

    void remove(const QString &, Callback done) override
    {
        done({.ok = true});
    }
};

// A playlist of two channels; 'prefix' keeps the IDs of two playlists apart.
QByteArray playlist(const QString &prefix)
{
    QByteArray text = "#EXTM3U\n";

    for (int number = 1; number <= 2; ++number) {
        text += QStringLiteral("#EXTINF:-1 tvg-id=\"%1%2\" tvg-chno=\"%2\",%1 %2\n"
                               "http://streams.example/%1%2.ts\n")
                    .arg(prefix)
                    .arg(number)
                    .toUtf8();
    }

    return text;
}

// A playlist server that can be switched off, like a provider having a bad day.
struct Provider {
    explicit Provider(const QString &prefix)
        : server(
              [this, prefix](const FakeServer::Request &) -> FakeServer::Reply {
                  if (!up) {
                      return {.status = 503};
                  }

                  return {.body = playlist(prefix), .type = "audio/x-mpegurl"};
              })
    {
    }

    bool up = true;
    FakeServer server;

    [[nodiscard]] QString address() const
    {
        return server.address() + QStringLiteral("list.m3u");
    }
};

QVariantMap m3u(const QString &name, const Provider &provider)
{
    return {{QStringLiteral("type"), SourceConfig::M3u},
            {QStringLiteral("name"), name},
            {QStringLiteral("playlistUrl"), provider.address()}};
}

QString cacheFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/epg-cache.sqlite");
}
}

class TvhBackendTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_run = 0;

    // The settings file of the current test; kept apart from the one of the next.
    [[nodiscard]] QString settingsFile() const
    {
        return m_dir.filePath(QStringLiteral("flimmer-%1.conf").arg(m_run));
    }

    static QStringList uuids(const ChannelModel *channels)
    {
        QStringList list;

        for (int row = 0; row < channels->rowCount(); ++row) {
            list.append(channels->uuidAt(row));
        }

        return list;
    }

    static int numberOf(const ChannelModel *channels, const QString &uuid)
    {
        for (int row = 0; row < channels->rowCount(); ++row) {
            if (channels->uuidAt(row) == uuid) {
                return channels->get(row).value(QStringLiteral("number")).toInt();
            }
        }

        return -1;
    }

    static QVariantMap statusOf(const TvhBackend &backend, const QString &id)
    {
        for (const QVariant &entry : backend.sourceStatus()) {
            if (entry.toMap().value(QStringLiteral("id")).toString() == id) {
                return entry.toMap();
            }
        }

        return {};
    }

private Q_SLOTS:
    void init()
    {
        ++m_run;
        // A new cache for every test.
        QFile::remove(cacheFile());
        QFile::remove(cacheFile() + QStringLiteral("-wal"));
        QFile::remove(cacheFile() + QStringLiteral("-shm"));
    }

    void savingASourceKeepsTheOthers()
    {
        Provider one(QStringLiteral("A"));
        Provider two(QStringLiteral("B"));
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        const QString first = sources.save(m3u(QStringLiteral("One"), one));
        const QString second = sources.save(m3u(QStringLiteral("Two"), two));
        TvhBackend backend(&sources);

        QTRY_COMPARE_WITH_TIMEOUT(backend.channels()->rowCount(), 4, 10000);

        // Saving replaces the source that is running; the other one is not touched.
        sources.save({{QStringLiteral("id"), first}, {QStringLiteral("name"), QStringLiteral("1")}});

        QTRY_COMPARE_WITH_TIMEOUT(backend.channels()->rowCount(), 4, 10000);
        QCOMPARE(backend.sourceStatus().size(), 2);
        QCOMPARE(statusOf(backend, first).value(QStringLiteral("name")).toString(),
                 QStringLiteral("1"));
        QCOMPARE(statusOf(backend, second).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Two"));
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), TvhBackend::Online, 10000);
    }

    void savingADisabledSourceStartsNothing()
    {
        Provider one(QStringLiteral("A"));
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        const QString id = sources.save(m3u(QStringLiteral("One"), one));
        TvhBackend backend(&sources);

        QTRY_COMPARE_WITH_TIMEOUT(backend.channels()->rowCount(), 2, 10000);

        sources.save({{QStringLiteral("id"), id}, {QStringLiteral("enabled"), false}});

        QCOMPARE(backend.channels()->rowCount(), 0);
        QVERIFY(backend.sourceStatus().isEmpty());
        QCOMPARE(backend.status(), TvhBackend::Unconfigured);
    }

    void removingASourceDropsOnlyItsChannels()
    {
        Provider one(QStringLiteral("A"));
        Provider two(QStringLiteral("B"));
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        const QString first = sources.save(m3u(QStringLiteral("One"), one));
        const QString second = sources.save(m3u(QStringLiteral("Two"), two));
        TvhBackend backend(&sources);

        QTRY_COMPARE_WITH_TIMEOUT(backend.channels()->rowCount(), 4, 10000);

        sources.remove(second);

        QCOMPARE(backend.channels()->rowCount(), 2);
        QCOMPARE(backend.sourceStatus().size(), 1);

        for (const QString &uuid : uuids(backend.channels())) {
            QVERIFY(uuid.startsWith(first + QLatin1Char(':')));
        }
    }

    void reorderingRenumbersClashes()
    {
        Provider one(QStringLiteral("A"));
        Provider two(QStringLiteral("B"));
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        const QString first = sources.save(m3u(QStringLiteral("One"), one));
        const QString second = sources.save(m3u(QStringLiteral("Two"), two));
        TvhBackend backend(&sources);
        const ChannelModel *channels = backend.channels();

        QTRY_COMPARE_WITH_TIMEOUT(channels->rowCount(), 4, 10000);

        // The earlier source keeps its numbers, the later one gets the next free ones.
        QCOMPARE(numberOf(channels, first + QStringLiteral(":A1")), 1);
        QCOMPARE(numberOf(channels, first + QStringLiteral(":A2")), 2);
        QVERIFY(numberOf(channels, second + QStringLiteral(":B1")) > 2);
        QVERIFY(numberOf(channels, second + QStringLiteral(":B2")) > 2);

        sources.move(second, -1);

        QCOMPARE(numberOf(channels, second + QStringLiteral(":B1")), 1);
        QCOMPARE(numberOf(channels, second + QStringLiteral(":B2")), 2);
        QVERIFY(numberOf(channels, first + QStringLiteral(":A1")) > 2);
        QCOMPARE(backend.sourceStatus().first().toMap().value(QStringLiteral("id")).toString(),
                 second);
    }

    void eachSourcesStatusIsReported()
    {
        Provider one(QStringLiteral("A"));
        Provider two(QStringLiteral("B"));
        two.up = false;
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        const QString first = sources.save(m3u(QStringLiteral("One"), one));
        const QString second = sources.save(m3u(QStringLiteral("Two"), two));
        TvhBackend backend(&sources);

        QTRY_COMPARE_WITH_TIMEOUT(
            statusOf(backend, second).value(QStringLiteral("status")).toInt(),
            static_cast<int>(TvhBackend::Offline),
            10000);

        // One source works, so the whole is online, but the other one is still named.
        QCOMPARE(backend.status(), TvhBackend::Online);
        QVERIFY(backend.statusText().contains(QLatin1String("Two")));
        QCOMPARE(backend.channels()->rowCount(), 2);

        // The overall status stays the same when the second one comes up, but its line must not.
        two.up = true;
        QSignalSpy changed(&backend, &TvhBackend::sourceStatusChanged);
        backend.refresh();

        QTRY_COMPARE_WITH_TIMEOUT(
            statusOf(backend, second).value(QStringLiteral("status")).toInt(),
            static_cast<int>(TvhBackend::Online),
            10000);
        QVERIFY(changed.count() > 0);
        QCOMPARE(backend.channels()->rowCount(), 4);
        QCOMPARE(statusOf(backend, first).value(QStringLiteral("status")).toInt(),
                 static_cast<int>(TvhBackend::Online));
    }

    void anOfflineSourceStartsFromTheCache()
    {
        Provider one(QStringLiteral("A"));
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        const QString id = sources.save(m3u(QStringLiteral("One"), one));

        {
            TvhBackend first(&sources);
            QTRY_COMPARE_WITH_TIMEOUT(first.channels()->rowCount(), 2, 10000);
            // Written on the cache's own thread; the destructor waits for it.
        }

        one.up = false;
        TvhBackend backend(&sources);

        // The provider is down, but its channels are shown, and can be played.
        QTRY_COMPARE_WITH_TIMEOUT(backend.channels()->rowCount(), 2, 10000);
        QTRY_COMPARE_WITH_TIMEOUT(backend.status(), TvhBackend::Offline, 10000);
        QCOMPARE(backend.channels()->rowCount(), 2);

        QSignalSpy stream(&backend, &TvhBackend::streamReady);
        backend.requestStream(id + QStringLiteral(":A1"));
        QCOMPARE(stream.count(), 1);
    }

    void aRemovedSourceLeavesNoCache()
    {
        Provider one(QStringLiteral("A"));
        Provider two(QStringLiteral("B"));
        QSettings file(settingsFile(), QSettings::IniFormat);
        SourceSettings sources(&file, new EmptyStore);
        sources.save(m3u(QStringLiteral("One"), one));
        const QString second = sources.save(m3u(QStringLiteral("Two"), two));

        {
            TvhBackend first(&sources);
            QTRY_COMPARE_WITH_TIMEOUT(first.channels()->rowCount(), 4, 10000);
        }

        // Removed while Flimmer wasn't running; its channels must not come back.
        two.up = false;
        sources.remove(second);
        TvhBackend backend(&sources);

        QTRY_COMPARE_WITH_TIMEOUT(backend.channels()->rowCount(), 2, 10000);
        QTest::qWait(300);
        QCOMPARE(backend.channels()->rowCount(), 2);
    }
};

int main(int argc, char **argv)
{
    // Settings and the cache go to a folder of their own, not the user's.
    QTemporaryDir home;
    qputenv("HOME", home.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());

    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("flimmer-test"));
    QGuiApplication::setApplicationName(QStringLiteral("flimmer-test"));

    TvhBackendTest test;

    return QTest::qExec(&test, argc, argv);
}

#include "test_tvhbackend.moc"
