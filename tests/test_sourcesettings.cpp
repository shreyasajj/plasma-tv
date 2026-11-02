// SPDX-FileCopyrightText: 2026 shreyasajj
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "networkauth.h"
#include "sources/secretstore.h"
#include "sources/sourcesettings.h"

#include <QJsonDocument>
#include <QJsonObject>
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
    bool failWrites = false;

    void read(const QString &key, Callback done) override
    {
        done({.ok = true, .found = values.contains(key), .value = values.value(key)});
    }
    void write(const QString &key, const QString &value, Callback done) override
    {
        if (failWrites) {
            done({.ok = false, .error = QStringLiteral("locked")});
            return;
        }

        values.insert(key, value);
        done({.ok = true});
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

        // The password moved, and the old settings are gone.
        const QJsonObject secret =
            QJsonDocument::fromJson(
                store->values.value(QStringLiteral("source/%1/secret").arg(config.id)).toUtf8())
                .object();
        QCOMPARE(secret.value(QStringLiteral("password")).toString(), QStringLiteral("secret"));
        QVERIFY(!store->values.contains(QStringLiteral("server-password")));
        QVERIFY(!s->contains(QStringLiteral("server/host")));
        QCOMPARE(s->value(QStringLiteral("playback/lastChannel")).toString(),
                 config.id + QStringLiteral(":abc"));

        // Read back like at the next start.
        SourceSettings again(s.get(), store);
        QCOMPARE(again.configs().size(), 1);
        QCOMPARE(again.configs().first().id, config.id);
        QCOMPARE(again.configs().first().secret(QStringLiteral("password")),
                 QStringLiteral("secret"));

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
        store->failWrites = true;

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
        SourceSettings sources(s.get(), new MemoryStore);

        QCOMPARE(sources.configs().size(), 1);
        QVERIFY(!s->contains(QStringLiteral("server/host")));
        QCOMPARE(s->value(QStringLiteral("sources/size")).toInt(), 1);
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
        QVERIFY(store->values.contains(QStringLiteral("source/%1/secret").arg(second)));
        QVERIFY(!store->values.contains(QStringLiteral("source/%1/secret").arg(first)));

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
        QVERIFY(!store->values.contains(QStringLiteral("source/%1/secret").arg(second)));

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
};

QTEST_GUILESS_MAIN(SourceSettingsTest)

#include "test_sourcesettings.moc"
