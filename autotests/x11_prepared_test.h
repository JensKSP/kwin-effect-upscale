/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QList>
#include <QObject>
#include <QSize>
#include <QStringList>
#include <Qt>

#include <functional>
#include <utility>

// Stands in for a helper answering org.kde.KWin.Upscale.Helper1: it prepared
// every program it is asked about to render at `size`.
// One screen as the interface spells them, a(iiiii).
struct TestScreen
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int rate = 0;
};
Q_DECLARE_METATYPE(TestScreen)

inline QDBusArgument &operator<<(QDBusArgument &argument, const TestScreen &screen)
{
    argument.beginStructure();
    argument << screen.x << screen.y << screen.width << screen.height << screen.rate;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, TestScreen &screen)
{
    argument.beginStructure();
    argument >> screen.x >> screen.y >> screen.width >> screen.height >> screen.rate;
    argument.endStructure();
    // The signature QtDBus requires of a demarshaller returns its argument.
    // NOLINTNEXTLINE(bugprone-return-const-ref-from-parameter)
    return argument;
}

class TestHelper : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Upscale.Helper1")

public:
    QSize size;
    QList<uint> asked;
    QList<QSize> wanted;
    QStringList offers;
    int setupOffers = 0;
    // The screens the effect last sent, its own first.
    QList<TestScreen> offered;
    QStringList answers;
    QStringList restarts;
    std::function<void()> onPresent;

public Q_SLOTS:
    Q_SCRIPTABLE QString offerSetup(uint pid, const QString &windowClass, const QString &title, const QList<TestScreen> &screens, QString &question)
    {
        ++setupOffers;
        return offer(pid, windowClass, title, screens, question);
    }
    Q_SCRIPTABLE QString offer(uint pid, const QString &windowClass, const QString &title, const QList<TestScreen> &screens, QString &question)
    {
        Q_UNUSED(title)
        const TestScreen own = screens.value(0);
        offers.append(QStringLiteral("%1 %2 %3 %4 %5").arg(pid).arg(windowClass).arg(own.width).arg(own.height).arg(own.rate));
        offered = screens;
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
    Q_SCRIPTABLE QSize present(uint pid, const QString &windowClass, const QList<TestScreen> &asking)
    {
        Q_UNUSED(windowClass)
        asked.append(pid);
        const TestScreen own = asking.value(0);
        wanted.append(QSize(own.width, own.height));
        offered = asking;
        if (const auto beforeReply = std::exchange(onPresent, {})) {
            beforeReply();
        }
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
    void defersWineUntilPrepared_data();
    void defersWineUntilPrepared();
    void cleanup();
    void presentsAWindowAHelperPrepared();
    void keepsThePointerOverWhatItPresents();
    void keepsTheKeyboardWhereThePointerIs();
    void letsAPresentedGameLockThePointer();
    void leavesAWindowNobodyPrepared();
    void asksTheUserAndRestartsTheGame();
    void postponesWithEscape();
    void answersWithAClick();

private:
    QString status();
    QString driver(const QString &property);
    void request(const QString &name, const QByteArray &contents);
    void press(Qt::Key key);
    void registerHelper(TestHelper *helper);
    QStringList answers();
    void unregisterHelper();
    QDBusInterface m_effects{QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"),
                             QStringLiteral("org.kde.kwin.Effects"), QDBusConnection::sessionBus()};
};
