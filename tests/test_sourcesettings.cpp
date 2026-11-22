// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "networkauth.h"
#include "sources/secretstore.h"
#include "sources/sourcesettings.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace
{
// Answers right away, like a wallet that is already open.
class MemoryStore : public SecretStore
{
public:
    QHash<QString, QString> values;
    bool locked = false;
    int reads = 0;

    void read(const QString &key, Callback done) override
    {
        ++reads;

        if (locked) {
            done({.ok = false, .error = QStringLiteral("locked")});

            return;
        }

        done({.ok = true, .found = values.contains(key), .value = values.value(key)});
    }

    void remove(const QString &key, Callback done) override
    {
        values.remove(key);
        done({.ok = true});
    }
};
}

class SourceSettingsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    int m_file = 0;

    std::unique_ptr<QSettings> settings()
    {
        return std::make_unique<QSettings>(
            m_dir.filePath(QStringLiteral("flimmer-%1.conf").arg(m_file)),
            QSettings::IniFormat);
    }

    void writeOldServer(QSettings &s, bool withUser)
    {
        s.setValue(QStringLiteral("server/backend"), QStringLiteral("tvheadend"));
        s.setValue(QStringLiteral("server/host"), QStringLiteral("tv.local"));
        s.setValue(QStringLiteral("server/port"), 9981);
        s.setValue(QStringLiteral("server/username"), withUser ? QStringLiteral("me") : QString());
        s.setValue(QStringLiteral("playback/lastChannel"), QStringLiteral("abc"));
    }

private Q_SLOTS:
    void init()
    {
        ++m_file;
    }

    void startsEmpty()
    {
        auto s = settings();
        SourceSettings sources(s.get(), new MemoryStore);
        QVERIFY(sources.configs().isEmpty());
        QVERIFY(!sources.configured());
        QVERIFY(sources.credentialsReady());
    }

    void migratesOldServer()
    {
        auto s = settings();
        writeOldServer(*s, true);
        auto *store = new MemoryStore;
        store->values.insert(QStringLiteral("server-password"), QStringLiteral("secret"));

        SourceSettings sources(s.get(), store);

        QCOMPARE(sources.configs().size(), 1);
        const SourceConfig config = sources.configs().first();
        QCOMPARE(config.type, SourceConfig::Tvheadend);
        QCOMPARE(config.field(QStringLiteral("host")), QStringLiteral("tv.local"));
        QCOMPARE(config.secret(QStringLiteral("password")), QStringLiteral("secret"));
        QVERIFY(sources.credentialsReady());
        QVERIFY(sources.configured());

        // The password moved to the settings, and the old settings are gone.
        QCOMPARE(s->value(QStringLiteral("sources/1/secrets/password")).toString(),
                 QStringLiteral("secret"));
        QVERIFY(!store->values.contains(QStringLiteral("server-password")));
        QVERIFY(!s->contains(QStringLiteral("server/host")));
        QCOMPARE(s->value(QStringLiteral("playback/lastChannel")).toString(),
                 config.id + QStringLiteral(":abc"));

        // Read back like at the next start, without asking the secret store again.
        const int reads = store->reads;
        SourceSettings again(s.get(), store);
        QCOMPARE(again.configs().size(), 1);
        QCOMPARE(again.configs().first().id, config.id);
        QCOMPARE(again.configs().first().secret(QStringLiteral("password")),
                 QStringLiteral("secret"));
        QVERIFY(again.credentialsReady());
        QCOMPARE(store->reads, reads);

        // The server's credentials are answered to its host only.
        QCOMPARE(NetworkAuth::credentials().size(), 1);
        QCOMPARE(NetworkAuth::credentials().first().host, QStringLiteral("tv.local"));
    }

    void keepsOldServerWhenWalletFails()
    {
        auto s = settings();
        writeOldServer(*s, true);
        auto *store = new MemoryStore;
        store->values.insert(QStringLiteral("server-password"), QStringLiteral("secret"));
        store->locked = true;

        SourceSettings sources(s.get(), store);

        // Usable for now, but tried again next time.
        QCOMPARE(sources.configs().size(), 1);
        QVERIFY(!sources.secretStoreStatus().isEmpty());
        QVERIFY(s->contains(QStringLiteral("server/host")));
        QCOMPARE(s->value(QStringLiteral("sources/size")).toInt(), 0);
        QVERIFY(store->values.contains(QStringLiteral("server-password")));
    }

    void migratesWithoutPassword()
    {
        auto s = settings();
        writeOldServer(*s, false);
        auto *store = new MemoryStore;
        SourceSettings sources(s.get(), store);

        QCOMPARE(sources.configs().size(), 1);
        QVERIFY(!s->contains(QStringLiteral("server/host")));
        QCOMPARE(s->value(QStringLiteral("sources/size")).toInt(), 1);
        // Nothing to ask the secret store for.
        QCOMPARE(store->reads, 0);
    }

    void migratesPasswordFromSettings()
    {
        auto s = settings();
        writeOldServer(*s, true);
        s->setValue(QStringLiteral("server/password"), QStringLiteral("secret"));
        auto *store = new MemoryStore;
        SourceSettings sources(s.get(), store);

        QCOMPARE(sources.configs().size(), 1);
        QCOMPARE(sources.configs().first().secret(QStringLiteral("password")),
                 QStringLiteral("secret"));
        QVERIFY(sources.credentialsReady());
        QVERIFY(!s->contains(QStringLiteral("server/password")));
        QCOMPARE(s->value(QStringLiteral("sources/1/secrets/password")).toString(),
                 QStringLiteral("secret"));
        // The password was in the settings already.
        QCOMPARE(store->reads, 0);
    }

    void savesMovesAndRemoves()
    {
        auto s = settings();
        auto *store = new MemoryStore;
        SourceSettings sources(s.get(), store);
        QSignalSpy saved(&sources, &SourceSettings::saved);

        const QString first =
            sources.save({{QStringLiteral("type"), SourceConfig::M3u},
                          {QStringLiteral("name"), QStringLiteral("Playlist")},
                          {QStringLiteral("playlistUrl"), QStringLiteral("http://a/m3u")}});
        const QString second = sources.save({{QStringLiteral("type"), SourceConfig::Tvheadend},
                                             {QStringLiteral("name"), QStringLiteral("Server")},
                                             {QStringLiteral("host"), QStringLiteral("tv.local")},
                                             {QStringLiteral("username"), QStringLiteral("me")},
                                             {QStringLiteral("password"), QStringLiteral("pw")}});

        QCOMPARE(saved.count(), 2);
        QCOMPARE(sources.configs().size(), 2);
        // Different indexes keep their guide events apart.
        QVERIFY(sources.config(first).index != sources.config(second).index);
        // Secrets are kept in the settings, which only the user may read.
        QCOMPARE(s->value(QStringLiteral("sources/2/secrets/password")).toString(),
                 QStringLiteral("pw"));
        QVERIFY(!s->contains(QStringLiteral("sources/1/secrets/password")));
        QCOMPARE(QFile::permissions(s->fileName()),
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser
                     | QFileDevice::WriteUser);
        QCOMPARE(store->reads, 0);

        // The edit page learns whether a secret is set, never the secret.
        const QVariantMap page = sources.source(second);
        QCOMPARE(page.value(QStringLiteral("passwordSet")).toBool(), true);
        QVERIFY(!page.contains(QStringLiteral("password")));

        // Editing without touching the password keeps it.
        sources.save(
            {{QStringLiteral("id"), second}, {QStringLiteral("name"), QStringLiteral("Home")}});
        QCOMPARE(sources.config(second).secret(QStringLiteral("password")), QStringLiteral("pw"));
        QCOMPARE(sources.config(second).name, QStringLiteral("Home"));

        sources.move(second, -1);
        QCOMPARE(sources.configs().first().id, second);

        sources.remove(second);
        QCOMPARE(sources.configs().size(), 1);
        QVERIFY(!s->contains(QStringLiteral("sources/2/secrets/password")));

        SourceSettings again(s.get(), store);
        QCOMPARE(again.configs().size(), 1);
        QCOMPARE(again.configs().first().field(QStringLiteral("playlistUrl")),
                 QStringLiteral("http://a/m3u"));
    }

    void draftKeepsStoredSecrets()
    {
        auto s = settings();
        SourceSettings sources(s.get(), new MemoryStore);
        const QString id = sources.save({{QStringLiteral("type"), SourceConfig::Tvheadend},
                                         {QStringLiteral("host"), QStringLiteral("tv.local")},
                                         {QStringLiteral("username"), QStringLiteral("me")},
                                         {QStringLiteral("password"), QStringLiteral("pw")}});

        const SourceConfig draft =
            sources.draft({{QStringLiteral("id"), id}, {QStringLiteral("port"), 9982}});
        QCOMPARE(draft.secret(QStringLiteral("password")), QStringLiteral("pw"));
        QCOMPARE(draft.fields.value(QStringLiteral("port")).toInt(), 9982);
        // Not saved.
        QCOMPARE(sources.config(id).fields.value(QStringLiteral("port"), 9981).toInt(), 9981);
    }

    void validNeedsAnAddress()
    {
        auto s = settings();
        const SourceSettings sources(s.get(), new MemoryStore);

        QVERIFY(!sources.valid({{QStringLiteral("type"), SourceConfig::Tvheadend}}));
        QVERIFY(sources.valid({{QStringLiteral("type"), SourceConfig::Tvheadend},
                               {QStringLiteral("host"), QStringLiteral("tv.local")}}));
        QVERIFY(!sources.valid({{QStringLiteral("type"), SourceConfig::M3u}}));
        QVERIFY(sources.valid({{QStringLiteral("type"), SourceConfig::M3u},
                               {QStringLiteral("playlistUrl"), QStringLiteral("http://a/m3u")}}));
    }
};

QTEST_GUILESS_MAIN(SourceSettingsTest)

#include "test_sourcesettings.moc"
