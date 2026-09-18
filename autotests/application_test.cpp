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
    void describesEveryMethod();

private:
    void writeUserConfig(const QString &contents);
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
    // Nothing was found that works for it, and an entry must not imply one.
    QCOMPARE(racer->method, UpscaleControlMethod::None);
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

void ApplicationTest::describesEveryMethod()
{
    for (const UpscaleControlMethod method : {UpscaleControlMethod::None, UpscaleControlMethod::AdvertisedMode,
                                              UpscaleControlMethod::AdvertisedScale,
                                              UpscaleControlMethod::AdvertisedModeAndScale}) {
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
