// SPDX-FileCopyrightText: 2026 Jan Claußen <jan.claussen10@web.de>
//
// SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL

#include "channelmodel.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QTest>

namespace
{
Channel channel(const QString &uuid, int number, const QString &name, const QString &icon = {})
{
    Channel result;
    result.uuid = uuid;
    result.number = number;
    result.name = name;
    result.icon = icon;

    return result;
}

EpgEvent
makeEvent(qint64 id, const QString &channel, qint64 start, qint64 stop, const QString &title)
{
    EpgEvent result;
    result.id = id;
    result.channel = channel;
    result.start = start;
    result.stop = stop;
    result.title = title;

    return result;
}

class ChannelModelTest : public QObject
{
    Q_OBJECT

    EpgStore m_store;
    qint64 m_now = 0;

    static QVariant data(const ChannelModel &model, int row, ChannelModel::Role role)
    {
        return model.data(model.index(row), role);
    }

private Q_SLOTS:
    void init()
    {
        // Running for an hour, half of it over.
        m_now = QDateTime::currentSecsSinceEpoch();
        m_store.setEvents({
            makeEvent(1,
                      QStringLiteral("a"),
                      m_now - 1800,
                      m_now + 1800,
                      QStringLiteral("Now on A")),
            makeEvent(2,
                      QStringLiteral("a"),
                      m_now + 1800,
                      m_now + 3600,
                      QStringLiteral("Next on A")),
        });
    }

    void channelsAndLookups()
    {
        ChannelModel model(&m_store);
        QSignalSpy countSpy(&model, &ChannelModel::countChanged);

        model.setChannels({
            channel(QStringLiteral("a"), 1, QStringLiteral("Das Erste")),
            channel(QStringLiteral("b"), 2, QStringLiteral("ZDF")),
        });

        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(model.indexOfUuid(QStringLiteral("b")), 1);
        QCOMPARE(model.indexOfUuid(QStringLiteral("missing")), -1);
        QCOMPARE(model.indexOfNumber(2), 1);
        QCOMPARE(model.indexOfNumber(99), -1);
        QCOMPARE(model.uuidAt(0), QStringLiteral("a"));
        QVERIFY(model.uuidAt(5).isEmpty());

        // The same number of channels doesn't change the count.
        model.setChannels({
            channel(QStringLiteral("c"), 3, QStringLiteral("3sat")),
            channel(QStringLiteral("d"), 4, QStringLiteral("ARTE")),
        });

        QCOMPARE(countSpy.count(), 1);
        QCOMPARE(model.indexOfUuid(QStringLiteral("a")), -1);
        QCOMPARE(model.indexOfUuid(QStringLiteral("d")), 1);
    }

    void nowAndNext()
    {
        ChannelModel model(&m_store);
        model.setChannels({
            channel(QStringLiteral("a"), 1, QStringLiteral("Das Erste")),
            channel(QStringLiteral("b"), 2, QStringLiteral("ZDF")),
        });

        QCOMPARE(data(model, 0, ChannelModel::NowTitleRole).toString(), QStringLiteral("Now on A"));
        QCOMPARE(data(model, 0, ChannelModel::NextTitleRole).toString(),
                 QStringLiteral("Next on A"));
        QCOMPARE(data(model, 0, ChannelModel::NowIdRole).toLongLong(), 1);
        QCOMPARE(data(model, 0, ChannelModel::NowStopRole).toDouble(),
                 double(m_now + 1800) * 1000.0);
        QVERIFY(qAbs(data(model, 0, ChannelModel::NowProgressRole).toDouble() - 0.5) < 0.01);

        // Without guide data.
        QVERIFY(data(model, 1, ChannelModel::NowTitleRole).toString().isEmpty());
        QCOMPARE(data(model, 1, ChannelModel::NowIdRole).toInt(), -1);
        QCOMPARE(data(model, 1, ChannelModel::NowProgressRole).toDouble(), 0.0);
    }

    void get()
    {
        ChannelModel model(&m_store);
        model.setChannels({channel(QStringLiteral("a"), 1, QStringLiteral("Das Erste"))});

        const QVariantMap map = model.get(0);

        QCOMPARE(map.value(QStringLiteral("uuid")).toString(), QStringLiteral("a"));
        QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("Das Erste"));
        QCOMPARE(map.value(QStringLiteral("nowTitle")).toString(), QStringLiteral("Now on A"));
        QVERIFY(model.get(1).isEmpty());
        QVERIFY(model.get(-1).isEmpty());
    }

    void iconUrl()
    {
        ChannelModel model(&m_store);
        model.setChannels({
            channel(QStringLiteral("a"), 1, QStringLiteral("A"), QStringLiteral("imagecache/12")),
            channel(QStringLiteral("b"),
                    2,
                    QStringLiteral("B"),
                    QStringLiteral("/var/lib/logo.png")),
            channel(QStringLiteral("c"), 3, QStringLiteral("C")),
            channel(QStringLiteral("d"),
                    4,
                    QStringLiteral("D"),
                    QStringLiteral("http://logos/d.png")),
        });

        // Sources resolve their icons themselves: a relative address has nothing to resolve
        // against, absolute ones work.
        QVERIFY(model.iconUrl(0).isEmpty());
        QCOMPARE(model.iconUrl(3).toString(), QStringLiteral("http://logos/d.png"));
        // Paths on the server's disk can't be fetched.
        QVERIFY(model.iconUrl(1).isEmpty());
        QVERIFY(model.iconUrl(2).isEmpty());
    }

    void tickOnlyUpdatesProgress()
    {
        ChannelModel model(&m_store);
        model.setChannels({
            channel(QStringLiteral("a"), 1, QStringLiteral("A")),
            channel(QStringLiteral("b"), 2, QStringLiteral("B")),
        });

        QSignalSpy spy(&model, &ChannelModel::dataChanged);

        // Nothing started or ended since the last refresh.
        model.refreshNow(false);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).value<QModelIndex>().row(), 0);
        QCOMPARE(spy.first().at(1).value<QModelIndex>().row(), 1);
        QCOMPARE(spy.first().at(2).value<QList<int>>(), QList<int>{ChannelModel::NowProgressRole});
    }

    void guideChangeUpdatesEverything()
    {
        ChannelModel model(&m_store);
        model.setChannels({channel(QStringLiteral("a"), 1, QStringLiteral("A"))});
        QSignalSpy spy(&model, &ChannelModel::dataChanged);

        m_store.setEvents(
            {makeEvent(3, QStringLiteral("a"), m_now - 60, m_now + 60, QStringLiteral("New"))});
        model.refreshNow();

        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.first().at(2).value<QList<int>>().isEmpty());
        QCOMPARE(data(model, 0, ChannelModel::NowTitleRole).toString(), QStringLiteral("New"));
        QVERIFY(data(model, 0, ChannelModel::NextTitleRole).toString().isEmpty());
    }
};

}

QTEST_GUILESS_MAIN(ChannelModelTest)

#include "test_channelmodel.moc"
