/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

// Everything a settings test has to put in place before Qt or KConfig read
// anything: a configuration directory of its own, the application list this
// build installs underneath it, and a private bus. No test may reach a
// developer's session bus or their real configuration, and no real KWin may
// receive configuration or refresh calls from one.
template<typename Test>
int runSettingsTest(int argc, char **argv)
{
    QTemporaryDir configuration(QDir::currentPath() + QStringLiteral("/settings-test-XXXXXX"));
    if (!configuration.isValid()) {
        return 1;
    }
    // The page edits the application list a package installs, so the tests
    // read the real file: an entry that stops parsing, or a page that cannot
    // show it, is a shipped defect rather than a test problem.
    const QString defaults = configuration.filePath(QStringLiteral("defaults"));
    const QString user = configuration.filePath(QStringLiteral("user"));
    if (!QDir().mkpath(defaults) || !QDir().mkpath(user)) {
        return 1;
    }
    if (!QFile::copy(QStringLiteral(UPSCALE_APPLICATION_DEFAULTS), defaults + QStringLiteral("/kwinupscalerc"))) {
        return 1;
    }
    qputenv("XDG_CONFIG_DIRS", defaults.toLocal8Bit());
    qputenv("XDG_CONFIG_HOME", user.toLocal8Bit());
    qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent-upscale-test-bus");
    QProcess bus;
    bus.start(QStringLiteral("dbus-daemon"),
              {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
    if (!bus.waitForStarted() || !bus.waitForReadyRead()) {
        return 1;
    }
    const QByteArray address = bus.readLine().trimmed();
    if (address.isEmpty()) {
        return 1;
    }
    qputenv("DBUS_SESSION_BUS_ADDRESS", address);
    QApplication application(argc, argv);
    // After the application object, which Qt requires to be the first QObject
    // created and the last destroyed. Both are scoped here so that the test is
    // destroyed first, which is the order Qt supports.
    Test test;
    const int result = QTest::qExec(&test, argc, argv);
    bus.terminate();
    if (!bus.waitForFinished()) {
        bus.kill();
        bus.waitForFinished();
    }
    return result;
}

// The user's own layer of the application list, which every settings test
// starts without so that it begins from the list this build ships.
inline QString upscaleUserApplicationFile()
{
    return QString::fromLocal8Bit(qgetenv("XDG_CONFIG_HOME")) + QStringLiteral("/kwinupscalerc");
}
