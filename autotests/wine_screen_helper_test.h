/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "lockholder.h"
#include "wineprocess.h"
#include "wineregistry.h"
#include "winescreenhelper.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <unistd.h>

/*
 * The helper against real processes: a game is a process with Proton's
 * environment, and its Wine server is a process holding the lock below the
 * real /tmp, named after the test's prefix, so it meets nothing else there.
 */
class WineScreenHelperTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();
    void preparesTheGameAfterItsServerExits();
    void restartsTheGameWhenAsked_data();
    void restartsTheGameWhenAsked();
    void presentsOnlyWhatItPrepared();
    void resetsItsDesktop();
    void resetsItsDesktop_data();
    void remembersNever();
    void writesALostDesktopAgain();
    void writesALostDesktopAgain_data();
    void followsANewSize();
    void followsANewRate();
    void takesBackAPreparationAGameWillNotStartWith();
    void undoesWhatIsNoLongerWanted();
    void stopsWhenTheGameKeepsItsOwnResolution();

private:
    std::unique_ptr<QProcess> startGame() const;
    std::unique_ptr<LockHolder> startServer() const;
    std::optional<QSize> screen() const;
    void writePrefix() const;
    QString acceptOffer(QProcess &game);

    std::optional<QTemporaryDir> m_root;
    QString m_prefix;
    QString m_lockDirectory;
    std::optional<WineScreenRecords> m_records;
    std::optional<WineScreenHelper> m_helper;
    QStringList m_launched;
};
