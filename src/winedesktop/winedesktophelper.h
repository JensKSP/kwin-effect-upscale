/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "winedesktoprecord.h"

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
class WineDesktopHelper : public QObject
{
    Q_OBJECT

public:
    struct Offered
    {
        QString offer;
        QString question;
    };

    // How the game is started again; replaced in tests.
    using Launcher = std::function<bool(const WineDesktopRecord &record)>;

    explicit WineDesktopHelper(WineDesktopRecords *records, QObject *parent = nullptr);

    Offered offer(uint pid, const QString &windowClass, const QString &title, const QSize &size);
    QString answer(const QString &offer, const QString &answer);
    bool restart(const QString &offer);
    /*
     * The size the program in this window was prepared for, when it runs at it
     * now, so that the effect presents the window across its output. `wanted`
     * is the size the effect wants now: another one prepares the program for
     * it after this run, and none undoes the preparation after this run.
     */
    QSize present(uint pid, const QString &windowClass, const QSize &wanted);
    QList<WineDesktopRecord> prepared() const;
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
        WineDesktopRecord record;
        pid_t game = 0;
        pid_t server = 0;
        QDeadlineTimer expiry;
    };

    struct Job
    {
        QString id;
        QString offer;
        pid_t game = 0;
        pid_t server = 0;
        // Clearing writes `size` away; setting writes it.
        bool clear = false;
        QSize size;
        bool relaunch = false;
        QDeadlineTimer closeDeadline;
        bool terminated = false;
        // Held since the prefix was proven, while the game ran.
        std::shared_ptr<WineDirectory> directory;
    };

    void poll();
    bool advance(Job &job);
    static bool stillRunning(Job &job);
    void settle(const Job &job, WineDesktopRecord &record, WineWriteResult result);
    void startJob(const Job &job);
    void afterRun(const WineDesktopRecord &record, uint pid, pid_t server, const std::shared_ptr<WineDirectory> &directory,
                  const std::optional<QSize> &size);
    bool hasJob(const QString &id) const;

    WineDesktopRecords *m_records;
    QHash<QString, Pending> m_offers;
    QList<Job> m_jobs;
    QTimer m_timer;
    std::chrono::milliseconds m_closeGrace{20000};
    Launcher m_launcher;
};
