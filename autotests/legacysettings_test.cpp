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
#include "settings.h"

#include <KConfig>
#include <KConfigGroup>

#include <QFile>
#include <QTest>

using namespace KWin;

static QString userFile()
{
    return QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QLatin1String("/kwinupscalerc");
}

class LegacySettingsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void readsTheGlobalKeysOfThePreviousRelease();
    void keepsTheProfileKeysOfThePreviousRelease();

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
    const UpscaleApplication *old = upscaleApplicationForIdentity(QString(), QStringLiteral("old"));
    QVERIFY(old);
    QCOMPARE(old->overrides[std::size_t(UpscaleSetting::Resolution)], std::optional<int>(int(ResolutionPreset::Performance)));
    QVERIFY(!old->overrides[std::size_t(UpscaleSetting::MinimumPixels)]);
    // Automatic meant "follow the global resolution", which no value says now.
    const UpscaleApplication *automatic = upscaleApplicationForIdentity(QString(), QStringLiteral("automatic"));
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
    const UpscaleApplication *saved = upscaleApplicationForIdentity(QString(), QStringLiteral("old"));
    QVERIFY(saved);
    QCOMPARE(saved->methods[std::size_t(UpscalePresentation::X11FullScreen)], UpscaleMethod::X11Resize);
    QCOMPARE(saved->overrides[std::size_t(UpscaleSetting::Resolution)], std::optional<int>(int(ResolutionPreset::Performance)));
}

int runLegacySettingsTest(int argc, char *argv[])
{
    LegacySettingsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "legacysettings_test.moc"
