/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_prepared_test.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

namespace
{

// A game window made by a process of its own under a Wine loader's name, as
// x11_game_standin.cpp reports it. That process owns the X11 connection, which
// is how KWin 6.6 tells whose window it is, and names itself in _NET_WM_PID,
// which is how 6.3 did.
class StandInGame
{
public:
    StandInGame(const QString &program, const QString &when, const QSize &size)
    {
        m_process.start(program, {QStringLiteral("upscale-x11-test"), QString::number(size.width()), QString::number(size.height()), when});
    }
    ~StandInGame()
    {
        m_process.closeWriteChannel();
        if (!m_process.waitForFinished(5000)) {
            m_process.kill();
            m_process.waitForFinished();
        }
    }
    bool started()
    {
        return m_process.waitForStarted();
    }
    qint64 processId() const
    {
        return m_process.processId();
    }
    bool isFullscreen()
    {
        read();
        return m_fullscreen;
    }
    QRect geometry()
    {
        read();
        return m_geometry;
    }
    QList<QSize> configuredSizes()
    {
        read();
        return m_configured;
    }
    int closeRequests()
    {
        read();
        return m_closes;
    }

private:
    void read()
    {
        while (m_process.canReadLine()) {
            const QList<QByteArray> fields = m_process.readLine().simplified().split(' ');
            if (fields.size() == 2 && fields.first() == "fullscreen") {
                m_fullscreen = fields.at(1) == "1";
            } else if (fields.size() == 5 && fields.first() == "geometry") {
                m_geometry = QRect(fields.at(1).toInt(), fields.at(2).toInt(), fields.at(3).toInt(), fields.at(4).toInt());
            } else if (fields.size() == 3 && fields.first() == "configured") {
                m_configured.append(QSize(fields.at(1).toInt(), fields.at(2).toInt()));
            } else if (fields.first() == "close") {
                ++m_closes;
            }
        }
    }

    QProcess m_process;
    bool m_fullscreen = false;
    QRect m_geometry;
    QList<QSize> m_configured;
    int m_closes = 0;
};

} // namespace

void UpscaleX11PreparedTest::defersWineUntilPrepared_data()
{
    QTest::addColumn<QString>("runtime");
    QTest::addColumn<bool>("fullscreenOnMap");
    QTest::addColumn<bool>("available");
    QTest::addColumn<bool>("prepared");
    QTest::addColumn<bool>("disableBeforeReply");
    QTest::addColumn<bool>("disableBeforeOffer");
    QTest::newRow("wine-fullscreen") << QStringLiteral("wine") << true << true << false << false << false;
    QTest::newRow("wine64-later-fullscreen") << QStringLiteral("wine64") << false << true << false << false << false;
    QTest::newRow("preloader-no-helper") << QStringLiteral("wine-preloader") << true << false << false << false << false;
    QTest::newRow("proton-prepared-record-native-buffer") << QStringLiteral("wine64-preloader") << true << true << true << false << false;
    QTest::newRow("disabled-before-helper-reply") << QStringLiteral("wine64") << true << true << false << true << false;
    QTest::newRow("disabled-before-setup-offer") << QStringLiteral("wine64") << true << true << false << false << true;
}

void UpscaleX11PreparedTest::defersWineUntilPrepared()
{
    QFETCH(QString, runtime);
    QFETCH(bool, fullscreenOnMap);
    QFETCH(bool, available);
    QFETCH(bool, prepared);
    QFETCH(bool, disableBeforeReply);
    QFETCH(bool, disableBeforeOffer);
    const bool disabled = disableBeforeReply || disableBeforeOffer;
    // Give KWin a real process with a Wine loader basename, independently of
    // the game's class, that makes the game's window itself. No Wine
    // installation or prefix mutation is involved.
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString executable = directory.filePath(runtime);
    QVERIFY(QFile::copy(QStringLiteral(UPSCALE_TEST_X11_GAME), executable));
    TestHelper helper;
    if (disabled) {
        const auto disable = [this]() {
            KConfigGroup entry(KSharedConfig::openConfig(QStringLiteral("kwinupscalerc")), QStringLiteral("Application-test"));
            entry.writeEntry("Enabled", false);
            entry.sync();
            m_effects.call(QStringLiteral("reconfigureEffect"), QStringLiteral("upscale_test_driver"));
        };
        if (disableBeforeOffer) {
            helper.onOffer = disable;
        } else {
            helper.onPresent = disable;
        }
    }
    if (prepared) {
        helper.size = QSize(1920, 1080);
    }
    if (available) {
        registerHelper(&helper);
    }
    const auto unregister = qScopeGuard([this, available]() {
        if (available) {
            unregisterHelper();
        }
    });
    {
        StandInGame game(executable, fullscreenOnMap ? QStringLiteral("on-map") : QStringLiteral("after-map"),
                         QSize(3840, 2160));
        QVERIFY(game.started());
        QTRY_VERIFY(game.isFullscreen());
        if (available) {
            QTRY_COMPARE(helper.asked.size(), disabled ? 2 : 1);
            QCOMPARE(helper.asked.first(), uint(game.processId()));
            QCOMPARE(helper.wanted.first(), QSize(2560, 1440));
            if (disabled) {
                QVERIFY(helper.wanted.last().isEmpty());
                QCOMPARE(helper.offers.size(), disableBeforeOffer ? 1 : 0);
                QVERIFY(!status().contains(QStringLiteral("Set this game up?")));
            } else if (!prepared) {
                QTRY_COMPARE(helper.offers.size(), 1);
                QCOMPARE(helper.setupOffers, 1);
                QTRY_VERIFY(status().contains(QStringLiteral("Set this game up?")));
                press(Qt::Key_Return);
                QTRY_COMPARE(helper.answers, QStringList{QStringLiteral("offer-1 accept")});
                QTRY_VERIFY(status().contains(QStringLiteral("Restart it?")));
                press(Qt::Key_Escape);
                QVERIFY(helper.restarts.isEmpty());
            }
        }
        // Outlast ordinary resize validation. Neither a saved preparation nor
        // acceptance for a future launch may resize this native-size run.
        QTest::qWait(3500);
        QCOMPARE(game.geometry().size(), QSize(3840, 2160));
        QVERIFY(!game.configuredSizes().contains(QSize(2560, 1440)));
        QVERIFY(!game.configuredSizes().contains(QSize(1920, 1080)));
        QCOMPARE(game.closeRequests(), 0);
        if (prepared) {
            QVERIFY(helper.offers.isEmpty());
        }
    }
    if (prepared) {
        StandInGame restarted(executable, QStringLiteral("never"), QSize(1920, 1080));
        QVERIFY(restarted.started());
        QTRY_VERIFY(restarted.isFullscreen());
        QCOMPARE(restarted.geometry().size(), QSize(1920, 1080));
        QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    }
}
