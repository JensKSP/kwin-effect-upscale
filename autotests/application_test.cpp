/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "application.h"

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
static QString userDirectory()
{
    return QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME"));
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
    void matchesProgramsByFileName();
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
        // An entry has to constrain an identity, or it would match anything.
        QVERIFY(!application.windowClass.isEmpty() || !application.instance.isEmpty());
        // Every method acts before the window exists, so it has nothing but
        // the program name to recognize the application by.
        if (application.method != UpscaleControlMethod::None) {
            QVERIFY(!application.program.isEmpty());
        }
        // Matching order has to be decided by the file, not by its layout.
        QVERIFY(application.order > previousOrder);
        previousOrder = application.order;
    }
}

void ApplicationTest::matchesObservedIdentities()
{
    // The identities both games were observed to present on 2026-09-18.
    const UpscaleApplication *kart = upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"));
    QVERIFY(kart);
    QCOMPARE(kart->name, QStringLiteral("SuperTuxKart"));
    QCOMPARE(kart->method, UpscaleControlMethod::AdvertisedMode);
    // It must ask for something on its own, so that a fresh installation
    // already reduces a known game without the user configuring anything.
    QVERIFY(kart->preset != ResolutionPreset::Automatic);

    // Extreme Tux Racer carries its version in the window class, so it is
    // matched on the instance name and must keep matching when that class
    // changes with the next package.
    const UpscaleApplication *racer = upscaleApplicationForIdentity(QStringLiteral("Extreme Tux Racer 0.8.4"), QStringLiteral("etr"));
    QVERIFY(racer);
    QCOMPARE(racer->name, QStringLiteral("Extreme Tux Racer"));
    QCOMPARE(racer, upscaleApplicationForIdentity(QStringLiteral("Extreme Tux Racer 0.9.0"), QStringLiteral("etr")));
    QCOMPARE(racer->method, UpscaleControlMethod::X11Resize);
    QCOMPARE(racer->preset, ResolutionPreset::Quality);
    QVERIFY(racer->x11PrimaryOutputOnly);
}

// The two benchmarks are driven through the screen scale rather than the
// screen mode, because that is what their sources were observed to read.
void ApplicationTest::matchesTheBenchmarks()
{
    const UpscaleApplication *gl = upscaleApplicationForIdentity(QStringLiteral("com.github.glmark2.glmark2"),
                                                                 QStringLiteral("glmark2-wayland"));
    QVERIFY(gl);
    QCOMPARE(gl->method, UpscaleControlMethod::AdvertisedScale);
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("/usr/bin/glmark2-wayland")), gl);

    const UpscaleApplication *vk = upscaleApplicationForIdentity(QStringLiteral("com.github.vkmark.vkmark"),
                                                                 QStringLiteral("vkmark"));
    QVERIFY(vk);
    QCOMPARE(vk->method, UpscaleControlMethod::AdvertisedModeAndScale);

    // A benchmark exists to measure a machine, so neither may reduce anything
    // until the user asks: an unrequested reduction would make it report a
    // number for something nobody chose.
    QCOMPARE(gl->preset, ResolutionPreset::Automatic);
    QCOMPARE(vk->preset, ResolutionPreset::Automatic);
}

void ApplicationTest::ignoresIdentitiesItDoesNotKnow()
{
    QVERIFY(!upscaleApplicationForIdentity(QString(), QString()));
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("konsole"), QStringLiteral("konsole")));
    // Matching is case sensitive, as KWin's own exact window-class rules are.
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("SuperTuxKart"), QStringLiteral("SuperTuxKart")));
    // A stated field that differs refuses the entry even when the other matches.
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("launcher")));
}

void ApplicationTest::matchesProgramsByFileName()
{
    const UpscaleApplication *kart = upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"));
    // The same game sits in different directories depending on how it was
    // installed, so only the file name may decide.
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("/usr/games/supertuxkart")), kart);
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("/usr/local/bin/supertuxkart")), kart);
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("supertuxkart")), kart);
    QVERIFY(!upscaleApplicationForProgram(QString()));
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/usr/games/")));
    // A directory that merely contains the name is not the program.
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/opt/supertuxkart/launcher")));
}

// What a user changes has to win, and everything they did not change has to
// keep following the installed package, so that a later one can correct a
// method or add a game without disturbing their edits.
void ApplicationTest::layersUserChangesOverTheDefaults()
{
    const size_t shipped = upscaleApplications().size();
    writeUserConfig(QStringLiteral("[Application-supertuxkart]\n"
                                   "Preset=Performance\n"
                                   "\n"
                                   "[Application-glmark2]\n"
                                   "Enabled=false\n"
                                   "\n"
                                   "[Application-mygame]\n"
                                   "Name=My Game\n"
                                   "Instance=mygame\n"
                                   "Program=mygame\n"
                                   "Method=AdvertisedMode\n"
                                   "Preset=Balanced\n"
                                   "Order=5\n"));
    QCOMPARE(upscaleApplications().size(), shipped + 1);

    const UpscaleApplication *kart = upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"));
    QVERIFY(kart);
    QCOMPARE(kart->preset, ResolutionPreset::Performance);
    // The field the user did not touch still comes from the shipped file.
    QCOMPARE(kart->method, UpscaleControlMethod::AdvertisedMode);
    QVERIFY(kart->shipped);

    // A disabled entry stops matching without being deleted.
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("com.github.glmark2.glmark2"), QStringLiteral("glmark2-wayland")));
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/usr/bin/glmark2-wayland")));

    const UpscaleApplication *mine = upscaleApplicationForIdentity(QString(), QStringLiteral("mygame"));
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
                                   "Preset=Performance\n"
                                   "Method=None\n"
                                   "\n"
                                   "[Application-mygame]\n"
                                   "Name=My Game\n"
                                   "Instance=mygame\n"
                                   "Order=5\n"));
    QVERIFY(upscaleApplicationForIdentity(QString(), QStringLiteral("mygame")));

    upscaleRestoreApplications();

    // The list is the one this build ships: overridden fields are back and the
    // user's own application is gone.
    const UpscaleApplication *kart = upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"));
    QVERIFY(kart);
    QCOMPARE(kart->preset, ResolutionPreset::Quality);
    QCOMPARE(kart->method, UpscaleControlMethod::AdvertisedMode);
    QVERIFY(!upscaleApplicationForIdentity(QString(), QStringLiteral("mygame")));

    // Restoring must leave the defaults reachable rather than suppressed: a
    // deletion marker would hide them from every later package as well.
    upscaleReloadApplications();
    QVERIFY(upscaleApplicationForIdentity(QStringLiteral("com.github.vkmark.vkmark"), QStringLiteral("vkmark")));
    QCOMPARE(upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"))->preset,
             ResolutionPreset::Quality);
}

// An entry that names neither a window class nor an instance would match every
// window on the screen, the desktop included. Reading it as "recognize
// everything" is the one reading that cannot be right.
void ApplicationTest::dropsEntriesThatConstrainNothing()
{
    const size_t shipped = upscaleApplications().size();
    writeUserConfig(QStringLiteral("[Application-nothing]\n"
                                   "Name=Constrains Nothing\n"
                                   "Program=nothing\n"
                                   "Order=5\n"));
    QCOMPARE(upscaleApplications().size(), shipped);
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/usr/bin/nothing")));
}

// The keys are the file's vocabulary, so what is read has to be what is
// written back: a method stored under a name the reader does not know would
// silently become None the next time the file is read.
void ApplicationTest::spellsEveryMethodAndPreset()
{
    for (const UpscaleControlMethod method : {UpscaleControlMethod::None, UpscaleControlMethod::AdvertisedMode,
                                              UpscaleControlMethod::AdvertisedScale,
                                              UpscaleControlMethod::AdvertisedModeAndScale, UpscaleControlMethod::X11Resize}) {
        const QString key = upscaleMethodKey(method);
        QVERIFY(!key.isEmpty());
        writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\nMethod=%1\n").arg(key));
        const UpscaleApplication *stored = upscaleApplicationForIdentity(QString(), QStringLiteral("roundtrip"));
        QVERIFY(stored);
        QCOMPARE(stored->method, method);
    }
    QCOMPARE(upscaleMethodKey(UpscaleControlMethod::None), QStringLiteral("None"));

    for (const ResolutionPreset preset : {ResolutionPreset::Automatic, ResolutionPreset::Native,
                                          ResolutionPreset::UltraQuality, ResolutionPreset::Quality,
                                          ResolutionPreset::Balanced, ResolutionPreset::Performance,
                                          ResolutionPreset::Custom}) {
        const QString key = upscalePresetKey(preset);
        QVERIFY(!key.isEmpty());
        writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\nPreset=%1\n").arg(key));
        const UpscaleApplication *stored = upscaleApplicationForIdentity(QString(), QStringLiteral("roundtrip"));
        QVERIFY(stored);
        QCOMPARE(stored->preset, preset);
    }
    QCOMPARE(upscalePresetKey(ResolutionPreset::Automatic), QStringLiteral("Automatic"));

    // A file from a later version can name a method this build does not
    // implement. Recognizing the application and asking it for nothing is the
    // only safe reading; refusing the entry would lose the identity as well.
    writeUserConfig(QStringLiteral("[Application-roundtrip]\nInstance=roundtrip\nMethod=SomethingLater\nPreset=Enormous\n"));
    const UpscaleApplication *later = upscaleApplicationForIdentity(QString(), QStringLiteral("roundtrip"));
    QVERIFY(later);
    QCOMPARE(later->method, UpscaleControlMethod::None);
    QCOMPARE(later->preset, ResolutionPreset::Automatic);
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

    const UpscaleApplication *shipped = upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart"));
    QVERIFY(shipped);
    const UpscaleApplication original = *shipped;
    UpscaleApplication edited = original;
    edited.preset = ResolutionPreset::Performance;
    edited.minimumPixels = 2073600;
    edited.enabled = false;
    edited.order = original.order + 1;
    upscaleSaveApplication(edited, original);
    upscaleSyncApplications();

    QVERIFY(upscaleApplicationsCustomized());
    const QString stored = QString::fromUtf8(readUserConfig());
    QVERIFY2(stored.contains(QStringLiteral("Preset=Performance")), qPrintable(stored));
    QVERIFY(stored.contains(QStringLiteral("MinimumPixels=2073600")));
    QVERIFY(stored.contains(QStringLiteral("Enabled=false")));
    QVERIFY(stored.contains(QStringLiteral("Order=%1").arg(original.order + 1)));
    // Untouched fields keep following the installed package rather than being
    // frozen into the user's file at today's values.
    QVERIFY2(!stored.contains(QStringLiteral("Method=")), qPrintable(stored));
    QVERIFY(!stored.contains(QStringLiteral("WindowClass=")));
    QVERIFY(!stored.contains(QStringLiteral("Program=")));
    QVERIFY(!stored.contains(QStringLiteral("Name=")));
    // A shipped entry carries the package's own description, so the user's
    // file must not copy it.
    QVERIFY(!stored.contains(QStringLiteral("Note=")));

    // The saved list is what the effect sees afterwards, without re-reading.
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("supertuxkart"), QStringLiteral("supertuxkart")));

    // An application the user added has nothing behind it, so it carries every
    // field it states, including its own description.
    UpscaleApplication added;
    added.id = upscaleNewApplicationId(QStringLiteral("My Game"));
    added.name = QStringLiteral("My Game");
    added.windowClass = QStringLiteral("mygame");
    added.instance = QStringLiteral("mygame");
    added.program = QStringLiteral("mygame");
    added.method = UpscaleControlMethod::AdvertisedScale;
    added.preset = ResolutionPreset::Balanced;
    added.note = QStringLiteral("Measured by me.");
    added.order = 200;
    upscaleSaveApplication(added, UpscaleApplication{});
    upscaleSyncApplications();

    const UpscaleApplication *mine = upscaleApplicationForIdentity(QStringLiteral("mygame"), QStringLiteral("mygame"));
    QVERIFY(mine);
    QCOMPARE(mine->name, QStringLiteral("My Game"));
    QCOMPARE(mine->method, UpscaleControlMethod::AdvertisedScale);
    QCOMPARE(mine->preset, ResolutionPreset::Balanced);
    QCOMPARE(mine->note, QStringLiteral("Measured by me."));
    QVERIFY(!mine->shipped);
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("/opt/games/mygame")), mine);
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
    QVERIFY(upscaleApplicationForIdentity(QString(), QStringLiteral("mygame")));

    upscaleDeleteApplication(QStringLiteral("mygame"));
    upscaleSyncApplications();
    QVERIFY(!upscaleApplicationForIdentity(QString(), QStringLiteral("mygame")));
    QVERIFY(!QString::fromUtf8(readUserConfig()).contains(QStringLiteral("Application-mygame")));

    // A shipped entry cannot be removed: the next package brings it back, so
    // deleting it here would promise something that does not happen. It is
    // switched off instead, which is what the user's file can record.
    upscaleDeleteApplication(QStringLiteral("vkmark"));
    upscaleSyncApplications();
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("com.github.vkmark.vkmark"), QStringLiteral("vkmark")));
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
    QVERIFY(!upscaleUnknownApplication());
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/usr/bin/something-nobody-measured")));

    upscaleSetUnknownApplications(true, ResolutionPreset::Balanced);
    const UpscaleApplication *unknown = upscaleUnknownApplication();
    QVERIFY(unknown);
    QVERIFY(!unknown->name.isEmpty());
    QCOMPARE(unknown->preset, ResolutionPreset::Balanced);
    // The mode is the one request observed to leave a window covering the
    // screen whether or not the client acts on it, which is the only thing to
    // ask of an application nobody measured.
    QCOMPARE(unknown->method, UpscaleControlMethod::AdvertisedMode);
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("/usr/bin/something-nobody-measured")), unknown);
    // It describes a setting rather than an entry, so it must never reach the
    // list the editor writes back.
    QVERIFY(std::ranges::none_of(upscaleApplications(), [](const UpscaleApplication &application) {
        return application.id.isEmpty();
    }));
    // A measured entry still decides first.
    QCOMPARE(upscaleApplicationForProgram(QStringLiteral("/usr/games/supertuxkart"))->name, QStringLiteral("SuperTuxKart"));
    // And an entry the user switched off is not an unlisted application: it is
    // one they said to leave alone. Asking it for a resolution through this
    // setting would undo the only thing switching it off does.
    writeUserConfig(QStringLiteral("[Application-supertuxkart]\nEnabled=false\n"));
    upscaleSetUnknownApplications(true, ResolutionPreset::Balanced);
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/usr/games/supertuxkart")));
    // A program nothing in the list describes is still asked.
    QVERIFY(upscaleApplicationForProgram(QStringLiteral("/usr/bin/something-nobody-measured")));
    QFile::remove(userDirectory() + QLatin1String("/kwinupscalerc"));
    upscaleReloadApplications();
    // It has no window identity, so it never matches a window either.
    QVERIFY(!upscaleApplicationForIdentity(QStringLiteral("konsole"), QStringLiteral("konsole")));

    upscaleSetUnknownApplications(false, ResolutionPreset::Balanced);
    QVERIFY(!upscaleUnknownApplication());
    QVERIFY(!upscaleApplicationForProgram(QStringLiteral("/usr/bin/something-nobody-measured")));
}

void ApplicationTest::describesEveryMethod()
{
    for (const UpscaleControlMethod method : {UpscaleControlMethod::None, UpscaleControlMethod::AdvertisedMode,
                                              UpscaleControlMethod::AdvertisedScale,
                                              UpscaleControlMethod::AdvertisedModeAndScale, UpscaleControlMethod::X11Resize}) {
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
    return QTest::qExec(&test, argc, argv);
}

#include "application_test.moc"
