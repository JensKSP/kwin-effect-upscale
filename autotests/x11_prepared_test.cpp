/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "x11_client.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QSaveFile>
#include <QTest>

// Stands in for a helper answering org.kde.KWin.Upscale.Helper1: it prepared
// every program it is asked about to render at `size`.
class TestHelper : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Upscale.Helper1")

public:
    QSize size;
    QList<uint> asked;
    QList<QSize> wanted;
    QStringList offers;
    QStringList answers;
    QStringList restarts;

public Q_SLOTS:
    Q_SCRIPTABLE QString offer(uint pid, const QString &windowClass, const QString &title, const QSize &size, int refreshRate, QString &question)
    {
        Q_UNUSED(title)
        offers.append(QStringLiteral("%1 %2 %3 %4 %5").arg(pid).arg(windowClass).arg(size.width()).arg(size.height()).arg(refreshRate));
        question = QStringLiteral("Set this game up?");
        return QStringLiteral("offer-1");
    }
    Q_SCRIPTABLE QString answer(const QString &offer, const QString &answer)
    {
        answers.append(offer + QLatin1Char(' ') + answer);
        return answer == QStringLiteral("accept") ? QStringLiteral("Restart it?") : QString();
    }
    Q_SCRIPTABLE bool restart(const QString &offer)
    {
        restarts.append(offer);
        return true;
    }
    Q_SCRIPTABLE QSize present(uint pid, const QString &windowClass, const QSize &asking, int refreshRate)
    {
        Q_UNUSED(windowClass)
        Q_UNUSED(refreshRate)
        asked.append(pid);
        wanted.append(asking);
        return size;
    }
};

/*
 * A window whose program a helper prepared to render smaller: an ordinary
 * window at that size, which the effect makes fullscreen, holds at its size
 * and presents across the output.
 */
class UpscaleX11PreparedTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void presentsAWindowAHelperPrepared();
    void keepsThePointerOverWhatItPresents();
    void leavesAWindowNobodyPrepared();
    void asksTheUserAndRestartsTheGame();
    void postponesWithEscape();
    void answersWithAClick();

private:
    QString status();
    void request(const QString &name, const QByteArray &contents);
    void press(Qt::Key key);
    void registerHelper(TestHelper *helper);
    QStringList answers();
    void unregisterHelper();
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};

QString UpscaleX11PreparedTest::status()
{
    const QDBusReply<QString> reply = m_effects.call(QStringLiteral("supportInformation"), QStringLiteral("upscale_test_driver"));
    return reply.isValid() ? reply.value() : reply.error().message();
}

void UpscaleX11PreparedTest::init()
{
    QTRY_VERIFY(m_effects.isValid());
    // The effect only asks about a program it acts on.
    QFile catalogue(QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc"));
    QVERIFY(catalogue.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(catalogue.write("[Application-test]\nName=X11 test\nWindowClass=upscale-x11-test\nEnabled=true\n") > 0);
    catalogue.close();
    KSharedConfig::openConfig(QStringLiteral("kwinupscalerc"))->reparseConfiguration();
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("Osd", false);
    group.writeEntry("MinimumPixels", 1920 * 1080);
    group.sync();
    const QDBusReply<bool> loaded = m_effects.call(QStringLiteral("loadEffect"), QStringLiteral("upscale_test_driver"));
    QVERIFY(loaded.isValid() && loaded.value());
}

void UpscaleX11PreparedTest::cleanup()
{
    m_effects.call(QStringLiteral("unloadEffect"), QStringLiteral("upscale_test_driver"));
}

void UpscaleX11PreparedTest::presentsAWindowAHelperPrepared()
{
    TestHelper helper;
    helper.size = QSize(1920, 1080);
    registerHelper(&helper);

    // A Wine virtual desktop is an ordinary decorated window of the prepared
    // size, and its program never asks for fullscreen.
    X11Client game(false);
    game.reportProcess();
    QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(100, 100, 1920, 1080), false));
    QTRY_VERIFY_WITH_TIMEOUT(game.isFullscreen(), 10000);
    QCOMPARE(helper.asked.value(0), uint(QCoreApplication::applicationPid()));
    // The helper is told what the effect wants now: Quality on a 4K output.
    QCOMPARE(helper.wanted.value(0), QSize(2560, 1440));
    QCOMPARE(game.geometry().size(), QSize(1920, 1080));
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Supplied input: 1920 × 1080")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("Destination: 3840 × 2160")), qPrintable(status()));

    // Taking it out of fullscreen gives it back to its user for good.
    game.fullscreen(false);
    QTRY_VERIFY_WITH_TIMEOUT(!game.isFullscreen(), 10000);
    QTest::qWait(1000);
    QVERIFY(!game.isFullscreen());
    QCOMPARE(game.geometry().size(), QSize(1920, 1080));
    unregisterHelper();
}

// The pointer over the part of the output a presented window covers on screen
// but its own X11 window does not. KWin's hit test goes through the client's
// input region and finds whatever lies under the game there - in a session the
// desktop, here a window of its own - so without this the pointer, and a click
// with it, would go to that window instead of the game.
void UpscaleX11PreparedTest::keepsThePointerOverWhatItPresents()
{
    TestHelper helper;
    helper.size = QSize(1920, 1080);
    registerHelper(&helper);

    X11Client below(false);
    QVERIFY(below.show(QByteArrayLiteral("upscale-x11-below"), QRect(0, 0, 3840, 2160), false));
    X11Client game(false);
    game.reportProcess();
    QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 1920, 1080), false));
    QTRY_VERIFY_WITH_TIMEOUT(game.isFullscreen(), 10000);
    QTRY_VERIFY2(status().contains(QStringLiteral("presented by this effect")), qPrintable(status()));

    // Three quarters across the output, which is outside the game's own
    // 1920 x 1080 window and inside the picture it is presented as.
    request(QStringLiteral("upscale-test-pointer"), QByteArrayLiteral("2880 1620"));
    QTRY_COMPARE(game.lastMotion(), QPoint(1440, 810));

    // And a click there is the game's, not the window under it. The window
    // under it does see the pointer enter, because KWin's own hit test focuses
    // it before this effect points the seat at the game, which no filter can
    // prevent; what it must never see is the click.
    request(QStringLiteral("upscale-test-click"), QByteArrayLiteral("2880 1620"));
    QTRY_COMPARE(game.presses(), 1);
    QCOMPARE(game.lastPress(), QPoint(1440, 810));
    QCOMPARE(below.presses(), 0);
}

void UpscaleX11PreparedTest::leavesAWindowNobodyPrepared()
{
    // Without a helper nobody answers, and the window stays as it is.
    X11Client window(false);
    QVERIFY(window.show(QByteArrayLiteral("upscale-x11-test"), QRect(100, 100, 1920, 1080), false));
    QTest::qWait(2000);
    QVERIFY(!window.isFullscreen());
    QCOMPARE(window.geometry().size(), QSize(1920, 1080));
}

// Read and removed by the test driver inside the compositor, like the pointer
// request of the other X11 test; the next one waits until it has been taken.
void UpscaleX11PreparedTest::request(const QString &name, const QByteArray &contents)
{
    const QString path = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR")) + QLatin1Char('/') + name;
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(contents) > 0);
    QVERIFY(file.commit());
    QTRY_VERIFY(!QFile::exists(path));
}

void UpscaleX11PreparedTest::press(Qt::Key key)
{
    request(QStringLiteral("upscale-test-key"), QByteArray::number(int(key)));
}

void UpscaleX11PreparedTest::registerHelper(TestHelper *helper)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QVERIFY(bus.registerObject(QStringLiteral("/Helper"), helper, QDBusConnection::ExportScriptableSlots));
    QVERIFY(bus.registerService(QStringLiteral("org.kde.KWin.Upscale.Helper")));
}

void UpscaleX11PreparedTest::unregisterHelper()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QStringLiteral("/Helper"));
    QVERIFY(bus.unregisterService(QStringLiteral("org.kde.KWin.Upscale.Helper")));
}

void UpscaleX11PreparedTest::asksTheUserAndRestartsTheGame()
{
    TestHelper helper;
    registerHelper(&helper);
    X11Client game(false);
    game.reportProcess();
    QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160), true));
    QTRY_VERIFY_WITH_TIMEOUT(game.isFullscreen(), 10000);

    // The game goes on drawing at the output's size, as validation would find
    // of a Wine game in exclusive fullscreen: the helper is asked.
    request(QStringLiteral("upscale-test-unfollowed"), QByteArrayLiteral("upscale-x11-test 1920 1080"));
    QTRY_COMPARE(helper.offers.size(), 1);
    // The rate the virtual output runs at, which the helper needs for the modes
    // it describes.
    QCOMPARE(helper.offers.first(), QStringLiteral("%1 upscale-x11-test 1920 1080 60").arg(QCoreApplication::applicationPid()));
    QTRY_VERIFY2(status().contains(QStringLiteral("Set this game up?")), qPrintable(status()));

    // The first answer is selected; moving away and back leaves it selected.
    press(Qt::Key_Right);
    press(Qt::Key_Left);
    press(Qt::Key_Return);
    QTRY_COMPARE(helper.answers, QStringList{QStringLiteral("offer-1 accept")});
    QTRY_VERIFY2(status().contains(QStringLiteral("Restart it?")), qPrintable(status()));

    // Restarting asks the window to close and leaves the rest to the helper.
    press(Qt::Key_Return);
    QTRY_COMPARE(helper.restarts, QStringList{QStringLiteral("offer-1")});
    QTRY_COMPARE(game.closeRequests(), 1);
    QVERIFY2(!status().contains(QStringLiteral("Restart it?")), qPrintable(status()));
    unregisterHelper();
}

void UpscaleX11PreparedTest::postponesWithEscape()
{
    TestHelper helper;
    registerHelper(&helper);
    X11Client game(false);
    QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160), true));
    QTRY_VERIFY_WITH_TIMEOUT(game.isFullscreen(), 10000);
    request(QStringLiteral("upscale-test-unfollowed"), QByteArrayLiteral("upscale-x11-test 1920 1080"));
    QTRY_VERIFY2(status().contains(QStringLiteral("Set this game up?")), qPrintable(status()));
    press(Qt::Key_Escape);
    QTRY_COMPARE(helper.answers, QStringList{QStringLiteral("offer-1 later")});
    QTest::qWait(500);
    QVERIFY2(!status().contains(QStringLiteral("Set this game up?")), qPrintable(status()));
    QVERIFY(helper.restarts.isEmpty());
    QCOMPARE(game.closeRequests(), 0);

    // The same window is not asked about again.
    request(QStringLiteral("upscale-test-unfollowed"), QByteArrayLiteral("upscale-x11-test 1920 1080"));
    QTest::qWait(500);
    QCOMPARE(helper.offers.size(), 1);
    unregisterHelper();
}

void UpscaleX11PreparedTest::answersWithAClick()
{
    TestHelper helper;
    registerHelper(&helper);
    X11Client game(false);
    QVERIFY(game.show(QByteArrayLiteral("upscale-x11-test"), QRect(0, 0, 3840, 2160), true));
    QTRY_VERIFY_WITH_TIMEOUT(game.isFullscreen(), 10000);
    request(QStringLiteral("upscale-test-unfollowed"), QByteArrayLiteral("upscale-x11-test 1920 1080"));
    QTRY_VERIFY2(status().contains(QStringLiteral("Set this game up?")), qPrintable(status()));
    // The three answers side by side, once they have been drawn; the third is
    // "Never for this game".
    QStringList areas;
    QTRY_VERIFY2((areas = answers()).size() == 3, qPrintable(status()));
    const QList<QString> never = areas.at(2).split(QLatin1Char(','));
    const QPointF middle(never.at(0).toDouble() + (never.at(2).toDouble() / 2), never.at(1).toDouble() + (never.at(3).toDouble() / 2));
    request(QStringLiteral("upscale-test-click"), QByteArray::number(middle.x()) + ' ' + QByteArray::number(middle.y()));
    QTRY_COMPARE(helper.answers, QStringList{QStringLiteral("offer-1 never")});
    QTRY_VERIFY2(!status().contains(QStringLiteral("Set this game up?")), qPrintable(status()));
    unregisterHelper();
}

// The answers' areas as the driver reports them, one per answer.
QStringList UpscaleX11PreparedTest::answers()
{
    for (const QString &line : status().split(QLatin1Char('\n'))) {
        if (line.startsWith(QStringLiteral("answers: "))) {
            const QString areas = line.mid(QStringLiteral("answers: ").size()).trimmed();
            return areas.isEmpty() ? QStringList() : areas.split(QLatin1Char(';'));
        }
    }
    return {};
}

QTEST_MAIN(UpscaleX11PreparedTest)

#include "x11_prepared_test.moc"
