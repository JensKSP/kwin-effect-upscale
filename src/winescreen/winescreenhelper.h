/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "wineregistry.h"
#include "winescreenrecord.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QObject>
#include <QTimer>

#include <chrono>
#include <functional>

/*
 * The helper the effect asks, through org.kde.KWin.Upscale.Helper1, when a Wine
 * or Proton game draws at the output's size whatever the effect asks for.
 *
 * It offers to give the game's prefix a virtual desktop of the size the effect
 * chose. After the user accepts, it waits until the game and its Wine server
 * have exited, writes the desktop into the prefix, and, if the user chose to
 * restart, starts the game again. It remembers what it wrote so that it only
 * ever undoes its own change, and it remembers "never".
 */
class WineScreenHelper : public QObject
{
    Q_OBJECT

public:
    struct Offered
    {
        QString offer;
        QString question;
    };

    // How the game is started again; replaced in tests.
    using Launcher = std::function<bool(const WineScreenRecord &record)>;

    explicit WineScreenHelper(WineScreenRecords *records, QObject *parent = nullptr);

    Offered offer(uint pid, const QString &windowClass, const QString &title, const QList<WineScreen> &screens);
    QString answer(const QString &offer, const QString &answer);
    bool restart(const QString &offer);
    /*
     * The size the program in this window was prepared for, when it runs at it
     * now, so that the effect presents the window across its output. `wanted`
     * is the screens the effect wants the program to see now, its own first:
     * others than those described prepare the program for them after this run,
     * and none at all undoes the preparation after this run.
     */
    QSize present(uint pid, const QString &windowClass, const QList<WineScreen> &wanted);
    QList<WineScreenRecord> prepared() const;
    bool reset(const QString &id);

    // Whether an offer awaits an answer or a prefix awaits its write; the
    // service stays while this holds.
    bool busy() const;

    void setLauncher(const Launcher &launcher);
    void setTiming(std::chrono::milliseconds poll, std::chrono::milliseconds closeGrace);

Q_SIGNALS:
    // A job ended; for tests and the log.
    void jobFinished(const QString &id, WineWriteResult result);

private:
    struct Pending
    {
        WineScreenRecord record;
        pid_t game = 0;
        pid_t server = 0;
        // The screens the program is to see, its own first.
        QList<WineScreen> screens;
        QDeadlineTimer expiry;
    };

    /*
     * The run that follows a preparation this companion started itself. A game
     * that will not start with the screen it was described - because the size
     * its own settings name is gone from the list - is the one case the
     * preparation cannot be judged by what it draws, since it draws nothing.
     * The run is watched instead: a server that comes and goes without the
     * effect ever asking about a window of that program takes the description
     * back and is not offered again.
     */
    struct Probation
    {
        QString id;
        // Whether a Wine server for the prefix was seen at all yet.
        bool started = false;
        QDeadlineTimer expiry;
    };

    struct Job
    {
        QString id;
        QString offer;
        pid_t game = 0;
        pid_t server = 0;
        // Clearing takes the described screens away; setting describes these,
        // the program's own first.
        bool clear = false;
        QList<WineScreen> screens;
        bool relaunch = false;
        QDeadlineTimer closeDeadline;
        bool terminated = false;
        // Held since the prefix was proven, while the game ran.
        std::shared_ptr<WineDirectory> directory;
        // Set when the write first finds the prefix busy after the program has
        // gone; a job does not wait on a prefix for ever.
        std::optional<QDeadlineTimer> giveUp;
        // Set when the program and its server were first seen gone: a server
        // writes the registry out as it exits, so the write waits a moment
        // longer than the process it watched.
        std::optional<QDeadlineTimer> settled;
    };

    std::optional<Pending> locate(uint pid, const QString &windowClass, const QString &title);
    void poll();
    // The watched run reported what it did, or nothing yet; true when it is over.
    bool watch(Probation &probation);
    void proven(const QString &id);
    bool advance(Job &job);
    static bool stillRunning(Job &job);
    static WineWriteResult writeScreen(const Job &job, const WineScreenRecord &record);
    void settle(const Job &job, WineScreenRecord &record, WineWriteResult result);
    void startJob(const Job &job);
    void afterRun(const WineScreenRecord &record, uint pid, pid_t server, const std::shared_ptr<WineDirectory> &directory,
                  const QList<WineScreen> &screens);
    bool hasJob(const QString &id) const;

    WineScreenRecords *m_records;
    QHash<QString, Pending> m_offers;
    QList<Job> m_jobs;
    QList<Probation> m_probation;
    QTimer m_timer;
    std::chrono::milliseconds m_closeGrace{20000};
    Launcher m_launcher;
};
