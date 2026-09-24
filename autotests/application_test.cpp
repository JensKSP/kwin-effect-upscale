/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"
#include "matching.h"
#include "settings.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <ranges>

using namespace KWin;

// The effect's own defaults are what a package installs, so the tests read the
// real file rather than a fixture of their own: an entry that stops parsing is
// a shipped defect, not a test problem.
int runLegacySettingsTest(int argc, char *argv[]);
int runMatchingTest(int argc, char *argv[]);

static QString userDirectory()
{
    return QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
}

// A window as the effect sees it: its program's path, where it resolved, and
// its class and instance.
static const UpscaleApplication *forWindow(const QString &executable, const QString &windowClass, const QString &instance)
{
    return upscaleApplicationFor({executable, windowClass, instance});
}

static const UpscaleApplication *forInstance(const QString &instance)
{
    return forWindow(QString(), QString(), instance);
}

static const UpscaleApplication *atBind(const QString &executable)
{
    return upscaleApplicationAtBind(executable).application;
}

static const UpscaleApplication *superTuxKart()
{
    return forWindow(QStringLiteral("/usr/games/supertuxkart"), QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"));
}

class ApplicationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void readsTheShippedDefaults();
    void matchesObservedIdentities();
    void matchesTheBenchmarks();
    void ignoresIdentitiesItDoesNotKnow();
    void layersUserChangesOverTheDefaults();
    void restoringDiscardsOnlyTheUserChanges();
    void dropsEntriesThatConstrainNothing();
    void spellsEveryMethodAndPreset();
    void namesNewApplicationsWithoutCollision();
    void storesOnlyTheFieldsTheUserChanged();
    void removesOwnEntriesAndDisablesShippedOnes();
    void asksUnlistedApplicationsOnlyWhenTurnedOn();
    void describesEveryMethod();

private:
    void writeUserConfig(const QString &contents);
    static QByteArray readUserConfig();
};

void ApplicationTest::init()
{
    // Each test starts from the defaults alone, with no user file at all.
    QFile::remove(userDirectory() + QLatin1String("/kwinupscalerc"));
    upscaleReloadApplications();
}

void ApplicationTest::writeUserConfig(const QString &contents)
{
    QFile file(userDirectory() + QLatin1String("/kwinupscalerc"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents.toUtf8()), qint64(contents.toUtf8().size()));
    file.close();
    upscaleReloadApplications();
}

QByteArray ApplicationTest::readUserConfig()
{
    QFile file(userDirectory() + QLatin1String("/kwinupscalerc"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void ApplicationTest::readsTheShippedDefaults()
{
    QVERIFY(!upscaleApplications().empty());
    int previousOrder = -1;
    for (const UpscaleApplication &application : upscaleApplications()) {
        QVERIFY(!application.id.isEmpty());
        QVERIFY(!application.name.isEmpty());
        QVERIFY(!application.version.isEmpty());
        QVERIFY(!application.note.isEmpty());
        // Every shipped entry comes from the effect's own file, so every one
        // of them has a default behind it to be restored to.
        QVERIFY(application.shipped);
        QVERIFY(application.enabled);
        // An entry has to constrain an identity, or it would match anything,
        // and every pattern it states has to be usable.
        QVERIFY2(upscaleIdentityProblem(application).isEmpty(), qPrintable(application.id));
        // An advertisement is made before the window exists, so it has
        // nothing but the program's path to recognize the application by,
        // and an entry that also names a window could not answer then.
        const bool advertises = std::ranges::any_of(application.methods, [](const std::optional<KWin::UpscaleMethod> &method) {
            return method == KWin::UpscaleMethod::AdvertisedMode || method == KWin::UpscaleMethod::AdvertisedScale
                || method == KWin::UpscaleMethod::AdvertisedModeAndScale;
        });
        if (advertises) {
            QVERIFY(!application.executable.isEmpty());
            QVERIFY(application.windowClass.isEmpty() && application.instance.isEmpty());
        }
        QVERIFY2(upscaleAdvertisementProblem(application).isEmpty(), qPrintable(application.id));
        // Matching order has to be decided by the file, not by its layout.
        QVERIFY(application.order > previousOrder);
        previousOrder = application.order;
    }
}

void ApplicationTest::matchesObservedIdentities()
{
    // The identities both games were observed to present on 2026-09-18.
    const UpscaleApplication *kart = superTuxKart();
    QVERIFY(kart);
    QCOMPARE(kart->name, QStringLiteral("SuperTuxKart"));
    QCOMPARE(kart->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedMode));
    // The other five are unmeasured and state nothing, so they follow the
    // global profile's answer, which is Auto unless a person chose another,
    // and not Off: the game has only ever been run one of the six ways.
    QVERIFY(!kart->methods[std::size_t(KWin::UpscalePresentation::X11FullScreen)]);
    QCOMPARE(KWin::upscaleMethodFor(kart, KWin::UpscalePresentation::X11FullScreen), KWin::UpscaleMethod::Auto);
    // What a reset returns a slot to: the package's measurement.
    QCOMPARE(kart->measured[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedMode));
    // It states no resolution of its own. A shipped resolution would be taste,
    // and it would stop the user's global setting ever reaching this game.
    QVERIFY(!kart->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
    // A fresh installation still reduces it, because the global default is a
    // reduction rather than the do-nothing the old Automatic was.
    QVERIFY(KWin::upscaleResolveSettings(kart).resolution() != ResolutionPreset::Native);

    // Extreme Tux Racer carries its version in the window class, so it is
    // matched on the instance name and must keep matching when that class
    // changes with the next package.
    // It states its window alone, so it is found whether or not its X11
    // window's PID resolved to a path.
    const UpscaleApplication *racer = forWindow(QString(), QStringLiteral("Extreme Tux Racer 0.8.4"), QStringLiteral("etr"));
    QVERIFY(racer);
    QCOMPARE(racer->name, QStringLiteral("Extreme Tux Racer"));
    QCOMPARE(racer, forWindow(QStringLiteral("/usr/games/etr"), QStringLiteral("Extreme Tux Racer 0.9.0"), QStringLiteral("etr")));
    QCOMPARE(racer->methods[std::size_t(KWin::UpscalePresentation::X11FullScreen)], std::optional(KWin::UpscaleMethod::X11Resize));
    QVERIFY(!racer->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
    QVERIFY(racer->x11PrimaryOutputOnly);
    QVERIFY(racer->x11RequiresEmulatedMode);
}

// The two benchmarks are driven through the screen scale rather than the
// screen mode, because that is what their sources were observed to read.
void ApplicationTest::matchesTheBenchmarks()
{
    const UpscaleApplication *gl = forWindow(QStringLiteral("/usr/bin/glmark2-wayland"), QStringLiteral("com.github.glmark2.glmark2"),
                                             QStringLiteral("glmark2-wayland"));
    QVERIFY(gl);
    QCOMPARE(gl->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedScale));
    QCOMPARE(atBind(QStringLiteral("/usr/bin/glmark2-wayland")), gl);

    const UpscaleApplication *vk = forWindow(QStringLiteral("/usr/bin/vkmark"), QStringLiteral("com.github.vkmark.vkmark"),
                                             QStringLiteral("vkmark"));
    QVERIFY(vk);
    QCOMPARE(vk->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedModeAndScale));

    // They state no resolution, as no shipped entry does: every entry follows
    // the global values until the user states one of its own.
    QVERIFY(!gl->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
    QVERIFY(!vk->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
}

void ApplicationTest::ignoresIdentitiesItDoesNotKnow()
{
    QVERIFY(!forWindow(QString(), QString(), QString()));
    QVERIFY(!forWindow(QStringLiteral("/usr/bin/konsole"), QStringLiteral("konsole"), QStringLiteral("konsole")));
    // Matching is case sensitive, as KWin's own exact window-class rules are.
    QVERIFY(!forWindow(QString(), QStringLiteral("ETR"), QStringLiteral("ETR")));
    // A stated field that differs refuses the entry even when the other matches.
    QVERIFY(!forWindow(QString(), QStringLiteral("hl2_linux"), QStringLiteral("launcher")));
    // SuperTuxKart states its program alone, so its window class without its
    // program is not SuperTuxKart: a path that did not resolve matches no
    // entry that states one.
    QVERIFY(!forWindow(QString(), QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart")));
}

// What a user changes has to win, and everything they did not change has to
// keep following the installed package, so that a later one can correct a
// method or add a game without disturbing their edits.
void ApplicationTest::layersUserChangesOverTheDefaults()
{
    const size_t shipped = upscaleApplications().size();
    writeUserConfig(QStringLiteral("[Application-supertuxkart]\n"
                                   "Resolution=Performance\n"
                                   "\n"
                                   "[Application-glmark2]\n"
                                   "Enabled=false\n"
                                   "\n"
                                   "[Application-mygame]\n"
                                   "Name=My Game\n"
                                   "Executable=.*/mygame\n"
                                   "ExecutableMatch=RegularExpression\n"
                                   "MethodWaylandFullScreen=AdvertisedMode\n"
                                   "Resolution=Balanced\n"
                                   "Order=5\n"));
    QCOMPARE(upscaleApplications().size(), shipped + 1);

    const UpscaleApplication *kart = superTuxKart();
    QVERIFY(kart);
    QCOMPARE(KWin::upscaleResolveSettings(kart).resolution(), ResolutionPreset::Performance);
    // The field the user did not touch still comes from the shipped file.
    QCOMPARE(kart->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedMode));
    QVERIFY(kart->shipped);

    // A disabled entry stops matching without being deleted.
    QVERIFY(!forWindow(QStringLiteral("/usr/bin/glmark2-wayland"), QStringLiteral("com.github.glmark2.glmark2"),
                       QStringLiteral("glmark2-wayland")));
    QVERIFY(!atBind(QStringLiteral("/usr/bin/glmark2-wayland")));

    const UpscaleApplication *mine = atBind(QStringLiteral("/home/player/mygame"));
    QVERIFY(mine);
    QCOMPARE(mine->name, QStringLiteral("My Game"));
    // Nothing shipped describes it, which is how restoring knows to remove it.
    QVERIFY(!mine->shipped);
    // Its order puts it before every shipped entry.
    QCOMPARE(upscaleApplications().front().id, QStringLiteral("mygame"));
}

void ApplicationTest::restoringDiscardsOnlyTheUserChanges()
{
    writeUserConfig(QStringLiteral("[Application-supertuxkart]\n"
                                   "Resolution=Performance\n"
                                   "Method=None\n"
                                   "\n"
                                   "[Application-mygame]\n"
                                   "Name=My Game\n"
                                   "Instance=mygame\n"
                                   "Order=5\n"));
    QVERIFY(forInstance(QStringLiteral("mygame")));

    upscaleRestoreApplications();

    // The list is the one this build ships: overridden fields are back and the
    // user's own application is gone.
    const UpscaleApplication *kart = superTuxKart();
    QVERIFY(kart);
    // The user's own resolution is gone, so this follows the global one again.
    QVERIFY(!kart->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
    QCOMPARE(kart->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedMode));
    QVERIFY(!forInstance(QStringLiteral("mygame")));

    // Restoring must leave the defaults reachable rather than suppressed: a
    // deletion marker would hide them from every later package as well.
    upscaleReloadApplications();
    QVERIFY(atBind(QStringLiteral("/usr/bin/vkmark")));
    QVERIFY(!superTuxKart()->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
}

// An entry that names no program, window class or instance would match every
// window on the screen, the desktop included. Reading it as "recognize
// everything" is the one reading that cannot be right.
void ApplicationTest::dropsEntriesThatConstrainNothing()
{
    const size_t shipped = upscaleApplications().size();
    writeUserConfig(QStringLiteral("[Application-nothing]\n"
                                   "Name=Constrains Nothing\n"
                                   "Executable=\n"
                                   "ExecutableMatch=RegularExpression\n"
                                   "Order=5\n"));
    QCOMPARE(upscaleApplications().size(), shipped);
    QVERIFY(!forWindow(QStringLiteral("/usr/bin/nothing"), QStringLiteral("nothing"), QStringLiteral("nothing")));
}

// The keys are the file's vocabulary, so what is read has to be what is
// written back: a method stored under a name the reader does not know would
// silently become None the next time the file is read.
void ApplicationTest::spellsEveryMethodAndPreset()
{
    using KWin::UpscaleMethod;
    using KWin::UpscalePresentation;
    for (const UpscaleMethod method : {UpscaleMethod::Off, UpscaleMethod::AdvertisedMode, UpscaleMethod::AdvertisedScale,
                                       UpscaleMethod::AdvertisedModeAndScale}) {
        const QString key = upscaleMethodKey(method);
        QVERIFY(!key.isEmpty());
        writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\nMethodWaylandFullScreen=%1\n").arg(key));
        const UpscaleApplication *stored = forInstance(QStringLiteral("roundtrip"));
        QVERIFY(stored);
        QCOMPARE(stored->methods[std::size_t(UpscalePresentation::WaylandFullScreen)], std::optional(method));
    }
    QCOMPARE(upscaleMethodKey(UpscaleMethod::Off), QStringLiteral("Off"));

    // A method a presentation cannot carry is a file naming an advertisement
    // for an X11 window. It reads as asking for nothing rather than as an
    // instruction this build would act on.
    writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\nMethodX11FullScreen=AdvertisedMode\n"));
    QCOMPARE(forInstance(QStringLiteral("roundtrip"))
                 ->methods[std::size_t(UpscalePresentation::X11FullScreen)],
             std::optional(UpscaleMethod::Off));

    for (const ResolutionPreset preset : {ResolutionPreset::Native, ResolutionPreset::UltraQuality,
                                          ResolutionPreset::Quality, ResolutionPreset::Balanced,
                                          ResolutionPreset::Performance, ResolutionPreset::Custom}) {
        const QString key = upscalePresetKey(preset);
        QVERIFY(!key.isEmpty());
        writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\nResolution=%1\n").arg(key));
        const UpscaleApplication *stored = forInstance(QStringLiteral("roundtrip"));
        QVERIFY(stored);
        QCOMPARE(KWin::upscaleResolveSettings(stored).resolution(), preset);
    }

    // A file from a later version can name a method this build does not
    // implement. Recognizing the application and asking it for nothing is the
    // only safe reading; refusing the entry would lose the identity as well.
    writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\n"
                                   "MethodWaylandFullScreen=SomethingLater\nResolution=Enormous\n"));
    const UpscaleApplication *later = forInstance(QStringLiteral("roundtrip"));
    QVERIFY(later);
    QCOMPARE(later->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)], std::optional(KWin::UpscaleMethod::Off));
    // An unreadable resolution falls back to what would have applied anyway
    // rather than inventing one, and the key is still present, so the profile
    // is still stating something rather than silently inheriting.
    QVERIFY(later->overrides[std::size_t(KWin::UpscaleSetting::Resolution)]);
}

void ApplicationTest::namesNewApplicationsWithoutCollision()
{
    // Readable, and free of anything a configuration group cannot hold.
    QCOMPARE(upscaleNewApplicationId(QStringLiteral("My Game 2!")), QStringLiteral("mygame2"));
    // A name that reduces to nothing still needs an identifier.
    QCOMPARE(upscaleNewApplicationId(QStringLiteral("***")), QStringLiteral("application"));
    QCOMPARE(upscaleNewApplicationId(QString()), QStringLiteral("application"));
    // An identifier already in the list would make the two entries share a
    // configuration group, so the second one is given a suffix.
    QCOMPARE(upscaleNewApplicationId(QStringLiteral("SuperTuxKart")), QStringLiteral("supertuxkart2"));
    writeUserConfig(QStringLiteral("[Application-supertuxkart2]\nName=Mine\nInstance=mine\n"));
    QCOMPARE(upscaleNewApplicationId(QStringLiteral("SuperTuxKart")), QStringLiteral("supertuxkart3"));
}

// Saving has to store the difference and not a copy of the shipped values, or
// the next package could no longer correct anything the user merely looked at.
void ApplicationTest::storesOnlyTheFieldsTheUserChanged()
{
    QVERIFY(!upscaleApplicationsCustomized());

    const UpscaleApplication *shipped = superTuxKart();
    QVERIFY(shipped);
    const UpscaleApplication original = *shipped;
    UpscaleApplication edited = original;
    edited.overrides[std::size_t(KWin::UpscaleSetting::Resolution)] = int(ResolutionPreset::Performance);
    edited.overrides[std::size_t(KWin::UpscaleSetting::MinimumPixels)] = 2073600;
    edited.enabled = false;
    edited.order = original.order + 1;
    upscaleSaveApplication(edited, original);
    upscaleSyncApplications();

    QVERIFY(upscaleApplicationsCustomized());
    const QString stored = QString::fromUtf8(readUserConfig());
    QVERIFY2(stored.contains(QStringLiteral("Resolution=Performance")), qPrintable(stored));
    QVERIFY(stored.contains(QStringLiteral("MinimumPixels=2073600")));
    QVERIFY(stored.contains(QStringLiteral("Enabled=false")));
    QVERIFY(stored.contains(QStringLiteral("Order=%1").arg(original.order + 1)));
    // Untouched fields keep following the installed package rather than being
    // frozen into the user's file at today's values.
    QVERIFY2(!stored.contains(QStringLiteral("Method=")), qPrintable(stored));
    QVERIFY(!stored.contains(QStringLiteral("WindowClass=")));
    QVERIFY(!stored.contains(QStringLiteral("Executable")));
    QVERIFY(!stored.contains(QStringLiteral("Match=")));
    QVERIFY(!stored.contains(QStringLiteral("Name=")));
    // A shipped entry carries the package's own description, so the user's
    // file must not copy it.
    QVERIFY(!stored.contains(QStringLiteral("Note=")));

    // The saved list is what the effect sees afterwards, without re-reading.
    QVERIFY(!superTuxKart());

    // An application the user added has nothing behind it, so it carries every
    // field it states, including its own description.
    UpscaleApplication added;
    added.id = upscaleNewApplicationId(QStringLiteral("My Game"));
    added.name = QStringLiteral("My Game");
    added.executable = QStringLiteral("/opt/games/.*game");
    added.executableMatch = UpscaleStringMatch::RegularExpression;
    added.windowClass = QStringLiteral("mygame");
    added.instance = QStringLiteral("mygame");
    added.methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)] = KWin::UpscaleMethod::AdvertisedScale;
    added.overrides[std::size_t(KWin::UpscaleSetting::Resolution)] = int(ResolutionPreset::Balanced);
    added.note = QStringLiteral("Measured by me.");
    added.order = 200;
    upscaleSaveApplication(added, UpscaleApplication{});
    upscaleSyncApplications();

    const QString storedAddition = QString::fromUtf8(readUserConfig());
    QVERIFY2(storedAddition.contains(QStringLiteral("ExecutableMatch=RegularExpression")), qPrintable(storedAddition));
    // Exact is what an absent match type means, so it is not written.
    QVERIFY(!storedAddition.contains(QStringLiteral("WindowClassMatch=")));
    const UpscaleApplication *mine = forWindow(QStringLiteral("/opt/games/mygame"), QStringLiteral("mygame"), QStringLiteral("mygame"));
    QVERIFY(mine);
    QCOMPARE(mine->name, QStringLiteral("My Game"));
    QCOMPARE(mine->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)],
             std::optional(KWin::UpscaleMethod::AdvertisedScale));
    QCOMPARE(KWin::upscaleResolveSettings(mine).resolution(), ResolutionPreset::Balanced);
    QCOMPARE(mine->note, QStringLiteral("Measured by me."));
    QVERIFY(!mine->shipped);
    // It names a window as well, so its path alone decides nothing at bind.
    QVERIFY(!upscaleApplicationAtBind(QStringLiteral("/opt/games/mygame")).decided);
}

void ApplicationTest::removesOwnEntriesAndDisablesShippedOnes()
{
    UpscaleApplication added;
    added.id = QStringLiteral("mygame");
    added.name = QStringLiteral("My Game");
    added.instance = QStringLiteral("mygame");
    added.order = 200;
    upscaleSaveApplication(added, UpscaleApplication{});
    upscaleSyncApplications();
    QVERIFY(forInstance(QStringLiteral("mygame")));

    upscaleDeleteApplication(QStringLiteral("mygame"));
    upscaleSyncApplications();
    QVERIFY(!forInstance(QStringLiteral("mygame")));
    QVERIFY(!QString::fromUtf8(readUserConfig()).contains(QStringLiteral("Application-mygame")));

    // A shipped entry cannot be removed: the next package brings it back, so
    // deleting it here would promise something that does not happen. It is
    // switched off instead, which is what the user's file can record.
    upscaleDeleteApplication(QStringLiteral("vkmark"));
    upscaleSyncApplications();
    QVERIFY(!atBind(QStringLiteral("/usr/bin/vkmark")));
    const std::vector<UpscaleApplication> &list = upscaleApplications();
    const auto disabled = std::ranges::find(list, QStringLiteral("vkmark"), &UpscaleApplication::id);
    QVERIFY(disabled != list.end());
    QVERIFY(!disabled->enabled);
    QVERIFY(disabled->shipped);
}

// Nothing is known in advance about a program nobody measured, so this is off
// unless the user asks for it, and a measured entry is never replaced by it.
void ApplicationTest::asksUnlistedApplicationsOnlyWhenTurnedOn()
{
    // A program nobody measured matches nothing at all. There is no longer a
    // synthesised entry standing in for one: a null profile is how a caller
    // asks for the global one, and the global profile answers for every window
    // no profile claimed.
    const UpscaleBindAnswer unmeasured = upscaleApplicationAtBind(QStringLiteral("/usr/bin/something-nobody-measured"));
    QVERIFY(unmeasured.decided);
    QVERIFY(!unmeasured.application);
    // Where nothing is stated, the global profile's six answers are Auto, as
    // a profile's are. What leaves such a program alone is the global
    // profile's own switch, which is off until someone turns it on.
    QVERIFY(std::ranges::all_of(upscaleGlobalMethods(), [](KWin::UpscaleMethod method) {
        return method == KWin::UpscaleMethod::Auto;
    }));
    QVERIFY(!upscaleResolveSettings(nullptr).acts());

    // A profile that is switched off takes no part in matching, by both
    // identities, so a later profile can claim the window and the global
    // profile gets it when none does. That is what makes switching one off
    // read as leaving the game alone: the global profile is off by default,
    // so nothing acts on it either.
    writeUserConfig(QStringLiteral("[Application-quiet]\nExecutable=/usr/bin/quiet\nEnabled=false\n"
                                   "[Application-quieter]\nInstance=quiet\nEnabled=false\n"));
    QVERIFY(!atBind(QStringLiteral("/usr/bin/quiet")));
    QVERIFY(upscaleApplicationAtBind(QStringLiteral("/usr/bin/quiet")).decided);
    QVERIFY(!forWindow(QStringLiteral("/usr/bin/quiet"), QString(), QStringLiteral("quiet")));

    // The single Method of a previous release lands in the one presentation it
    // can have been measured under, and nowhere else.
    writeUserConfig(QStringLiteral("[Application-old]\nInstance=old\nMethod=X11Resize\n"));
    const UpscaleApplication *old = forInstance(QStringLiteral("old"));
    QVERIFY(old);
    QCOMPARE(old->methods[std::size_t(KWin::UpscalePresentation::X11FullScreen)], std::optional(KWin::UpscaleMethod::X11Resize));
    QVERIFY(!old->methods[std::size_t(KWin::UpscalePresentation::WaylandFullScreen)]);

    // Except None, which recorded a decision about the program rather than
    // about one way of running it, so it fills every slot.
    writeUserConfig(QStringLiteral("[Application-none]\nInstance=none\nMethod=None\n"));
    const UpscaleApplication *none = forInstance(QStringLiteral("none"));
    QVERIFY(none);
    QVERIFY(std::ranges::all_of(none->methods, [](const std::optional<KWin::UpscaleMethod> &method) {
        return method == KWin::UpscaleMethod::Off;
    }));
}

void ApplicationTest::describesEveryMethod()
{
    for (const KWin::UpscaleMethod method : {KWin::UpscaleMethod::Auto, KWin::UpscaleMethod::Off,
                                             KWin::UpscaleMethod::AdvertisedMode, KWin::UpscaleMethod::AdvertisedScale,
                                             KWin::UpscaleMethod::AdvertisedModeAndScale,
                                             KWin::UpscaleMethod::X11Resize}) {
        QVERIFY(!describeControlMethod(method).isEmpty());
    }
}

// The configuration directories have to be in place before anything opens a
// configuration file, which is why this is not QTEST_GUILESS_MAIN.
int main(int argc, char *argv[])
{
    QTemporaryDir directory;
    if (!directory.isValid()) {
        return 1;
    }
    const QString defaults = directory.filePath(QStringLiteral("defaults"));
    const QString user = directory.filePath(QStringLiteral("user"));
    if (!QDir().mkpath(defaults) || !QDir().mkpath(user)) {
        return 1;
    }
    // The file the package installs, read from the source tree so that the
    // test covers the data this build actually ships.
    if (!QFile::copy(QStringLiteral(UPSCALE_APPLICATION_DEFAULTS), defaults + QLatin1String("/kwinupscalerc"))) {
        return 1;
    }
    qputenv("XDG_CONFIG_DIRS", defaults.toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", user.toLocal8Bit());
    QCoreApplication application(argc, argv);
    ApplicationTest test;
    const int result = QTest::qExec(&test, argc, argv);
    // The cases about what a previous release stored live in their own file,
    // beside the code that reads it, and run in the same environment. Both go
    // together once no installation can carry the old keys.
    return result | runLegacySettingsTest(argc, argv) | runMatchingTest(argc, argv);
}

#include "application_test.moc"
