/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What a previous release stored, read under the meaning it had then. Kept
// apart from application_test.cpp for the same reason legacysettings.cpp is
// kept apart from settings.cpp: both are temporary, and both go together once
// no installation can carry the old keys. It runs inside that test's main, in
// the configuration directories it prepares.

#include "application.h"
#include "legacysettings.h"
#include "matching.h"
#include "settings.h"

#include <KConfig>
#include <KConfigGroup>

#include <QFile>
#include <QTest>

#include <algorithm>
#include <ranges>

using namespace KWin;

static QString userFile()
{
    return QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QLatin1String("/kwinupscalerc");
}

static const UpscaleApplication *byInstance(const QString &instance)
{
    return upscaleApplicationFor({QString(), QString(), instance});
}

static const UpscaleApplication *byId(const QString &id)
{
    const std::vector<UpscaleApplication> &list = upscaleApplications();
    const auto found = std::ranges::find(list, id, &UpscaleApplication::id);
    return found == list.end() ? nullptr : &*found;
}

class LegacySettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void readsTheGlobalKeysOfThePreviousRelease();
    void keepsTheProfileKeysOfThePreviousRelease();
    void readsTheOldProgramKeyAsItWorked();

private:
    static void writeUserConfig(const QString &contents);
    static QByteArray readUserConfig();
};

void LegacySettingsTest::init()
{
    QFile::remove(userFile());
    upscaleReloadApplications();
}

void LegacySettingsTest::writeUserConfig(const QString &contents)
{
    QFile file(userFile());
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(contents.toUtf8()), qint64(contents.toUtf8().size()));
    file.close();
    upscaleReloadApplications();
}

QByteArray LegacySettingsTest::readUserConfig()
{
    QFile file(userFile());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// The global group of a previous release is read under the meaning it had
// then, for as long as no new key has replaced it. Nothing here is written:
// the compositor never rewrites a person's configuration on its own.
void LegacySettingsTest::readsTheGlobalKeysOfThePreviousRelease()
{
    KConfig file(QString(), KConfig::SimpleConfig);
    KConfigGroup global(&file, QStringLiteral("Effect-upscale"));

    // The old enumeration began with Automatic, so every stored number after
    // it names the preset one below it now. Automatic itself becomes no value:
    // it meant nobody had chosen, which an absent key says now, and turning it
    // into Native would stop the effect reducing the games it ships.
    global.writeEntry("Preset", 0);
    QVERIFY(!KWin::upscaleLegacyResolution(global));
    global.writeEntry("Preset", 1);
    QCOMPARE(KWin::upscaleLegacyResolution(global), std::optional<int>(int(ResolutionPreset::Native)));
    global.writeEntry("Preset", 5);
    QCOMPARE(KWin::upscaleLegacyResolution(global), std::optional<int>(int(ResolutionPreset::Performance)));
    global.writeEntry("Preset", 6);
    QCOMPARE(KWin::upscaleLegacyResolution(global), std::optional<int>(int(ResolutionPreset::Custom)));
    // The new key wins as soon as it exists: the old one is only a fallback.
    global.writeEntry("Resolution", int(ResolutionPreset::Balanced));
    QVERIFY(!KWin::upscaleLegacyResolution(global));

    QVERIFY(!KWin::upscaleLegacyUnlisted(global));
    global.writeEntry("UnknownApplications", true);
    QVERIFY(KWin::upscaleLegacyUnlisted(global));
    global.writeEntry("UnlistedApplications", false);
    QVERIFY(!KWin::upscaleLegacyUnlisted(global));

    // Only a stored off counts. The old default was on, so an absent key and a
    // stored true both said nothing the new model does not already say.
    QVERIFY(!KWin::upscaleLegacySwitchedOff(global));
    global.writeEntry("Enabled", true);
    QVERIFY(!KWin::upscaleLegacySwitchedOff(global));
    global.writeEntry("Enabled", false);
    QVERIFY(KWin::upscaleLegacySwitchedOff(global));

    // Applying the page removes what the new keys now carry in full, and keeps
    // the one thing no new key says: that upscaling was switched off.
    KWin::upscaleForgetLegacySettings(global);
    QVERIFY(!global.hasKey("Preset"));
    QVERIFY(!global.hasKey("UnknownApplications"));
    QVERIFY(global.hasKey("Enabled"));
}

// A profile's own old keys: read under their old meaning, and on the first save
// written under the current keys rather than dropped. The editor stores only
// what a person changed, so an unchanged value from an old key exists nowhere
// else, and deleting the key alone would lose it.
void LegacySettingsTest::keepsTheProfileKeysOfThePreviousRelease()
{
    using KWin::UpscaleMethod;
    using KWin::UpscalePresentation;
    using KWin::UpscaleSetting;

    // Preset named the resolution, and MinimumPixels=-1 meant "inherit". Read
    // as a number, -1 would clamp to zero and disable the threshold, scaling on
    // every output, which is the opposite of what it said.
    writeUserConfig(QStringLiteral("[Application-old]\nInstance=old\nMethod=X11Resize\n"
                                   "Preset=Performance\nMinimumPixels=-1\n"
                                   "[Application-automatic]\nInstance=automatic\nPreset=Automatic\n"));
    const UpscaleApplication *old = byInstance(QStringLiteral("old"));
    QVERIFY(old);
    QCOMPARE(old->overrides[std::size_t(UpscaleSetting::Resolution)], std::optional<int>(int(ResolutionPreset::Performance)));
    QVERIFY(!old->overrides[std::size_t(UpscaleSetting::MinimumPixels)]);
    // Automatic meant "follow the global resolution", which no value says now.
    const UpscaleApplication *automatic = byInstance(QStringLiteral("automatic"));
    QVERIFY(automatic);
    QVERIFY(!automatic->overrides[std::size_t(UpscaleSetting::Resolution)]);

    // Saved unchanged, which is what happens to every profile the first time a
    // person applies the page after the upgrade.
    const UpscaleApplication unchanged = *old;
    upscaleSaveApplication(unchanged, unchanged);
    upscaleSyncApplications();
    // Only the profile that was saved is rewritten; the other one keeps its old
    // keys until it is saved in turn, and is still read correctly meanwhile.
    const QByteArray stored = readUserConfig();
    const KConfig file(userFile(), KConfig::SimpleConfig);
    const KConfigGroup group(&file, QStringLiteral("Application-old"));
    QVERIFY2(!group.hasKey("Method"), stored.constData());
    QVERIFY2(!group.hasKey("Preset"), stored.constData());
    QCOMPARE(group.readEntry("MethodX11FullScreen", QString()), QStringLiteral("X11Resize"));
    QCOMPARE(group.readEntry("Resolution", QString()), QStringLiteral("Performance"));
    const UpscaleApplication *saved = byInstance(QStringLiteral("old"));
    QVERIFY(saved);
    QCOMPARE(saved->methods[std::size_t(UpscalePresentation::X11FullScreen)], UpscaleMethod::X11Resize);
    QCOMPARE(saved->overrides[std::size_t(UpscaleSetting::Resolution)], std::optional<int>(int(ResolutionPreset::Performance)));
}

// Program was consulted only at wl_output bind, and windows were found by their
// class and instance. Each old entry is read as the nearest two-gate entry
// that behaves the same, and saving it writes that under the current keys.
void LegacySettingsTest::readsTheOldProgramKeyAsItWorked()
{
    writeUserConfig(QStringLiteral("[Application-programonly]\nProgram=only\nOrder=1\n"
                                   "[Application-advertised]\nWindowClass=adv\nInstance=adv\nProgram=adv+plus\n"
                                   "MethodWaylandFullScreen=AdvertisedMode\nOrder=2\n"
                                   "[Application-resized]\nInstance=resized\nProgram=resized\n"
                                   "MethodX11FullScreen=X11Resize\nOrder=3\n"));
    // The program was all there was: it becomes the file name in any folder.
    const UpscaleApplication *only = byId(QStringLiteral("programonly"));
    QVERIFY(only);
    QCOMPARE(only->executable, QStringLiteral(".*/only"));
    QCOMPARE(only->executableMatch, UpscaleStringMatch::RegularExpression);
    QCOMPARE(upscaleApplicationAtBind(QStringLiteral("/usr/games/only")).application, only);

    // It was how an advertisement found its program. The window identity is
    // left out, because an entry that required a window could not advertise;
    // and the name is escaped, so a character a pattern treats specially is
    // matched as itself.
    const UpscaleApplication *advertised = byId(QStringLiteral("advertised"));
    QVERIFY(advertised);
    QVERIFY(advertised->windowClass.isEmpty() && advertised->instance.isEmpty());
    QCOMPARE(upscaleApplicationAtBind(QStringLiteral("/usr/bin/adv+plus")).application, advertised);
    QVERIFY(!upscaleApplicationAtBind(QStringLiteral("/usr/bin/advvplus")).application);

    // An X11 window was found by its identity and nothing was said at bind,
    // so the program decided nothing and is not required now either: the
    // window is found even when its PID does not resolve.
    const UpscaleApplication *resized = byId(QStringLiteral("resized"));
    QVERIFY(resized);
    QVERIFY(resized->executable.isEmpty());
    QCOMPARE(byInstance(QStringLiteral("resized")), resized);

    // Saved unchanged, each is written under the current keys, and the
    // window identity the reading left out is gone from the file too, or the
    // next reading would require it beside the path.
    for (const QString &id : {QStringLiteral("programonly"), QStringLiteral("advertised"), QStringLiteral("resized")}) {
        const UpscaleApplication unchanged = *byId(id);
        upscaleSaveApplication(unchanged, unchanged);
    }
    upscaleSyncApplications();
    const KConfig file(userFile(), KConfig::SimpleConfig);
    const QByteArray stored = readUserConfig();
    for (const QString &group : {QStringLiteral("Application-programonly"), QStringLiteral("Application-advertised"),
                                 QStringLiteral("Application-resized")}) {
        QVERIFY2(!KConfigGroup(&file, group).hasKey("Program"), stored.constData());
    }
    const KConfigGroup advertisedGroup(&file, QStringLiteral("Application-advertised"));
    QCOMPARE(advertisedGroup.readEntry("Executable", QString()), QStringLiteral(".*/adv\\+plus"));
    QCOMPARE(advertisedGroup.readEntry("ExecutableMatch", QString()), QStringLiteral("RegularExpression"));
    QVERIFY2(!advertisedGroup.hasKey("WindowClass") && !advertisedGroup.hasKey("Instance"), stored.constData());
    QVERIFY(!KConfigGroup(&file, QStringLiteral("Application-resized")).hasKey("Executable"));
    QCOMPARE(upscaleApplicationAtBind(QStringLiteral("/usr/bin/adv+plus")).application->id, QStringLiteral("advertised"));
    QCOMPARE(byInstance(QStringLiteral("resized"))->id, QStringLiteral("resized"));
}

int runLegacySettingsTest(int argc, char *argv[])
{
    LegacySettingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "legacysettings_test.moc"
