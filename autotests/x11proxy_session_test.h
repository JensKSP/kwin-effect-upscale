// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "session.h"
#include "x11proxy_session_fixture.h"

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantMap>
#include <QtGlobal>

#include <memory>

namespace UpscaleX11Test
{
// What the session logs, for a case that waits until it has said something.
inline QStringList s_logged;
inline QtMessageHandler s_passOn = nullptr;

inline void record(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    s_logged.append(message);
    s_passOn(type, context, message);
}
}

// The effect's side of the question, counting how often it is asked.
class EffectStandIn : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin.Effect.Upscale1")
public:
    int asked = 0;
    bool prefixMayMatch = true;
    // Programs whose names contain this are not in the list.
    QString unselected;
    QStringList lastCandidates;
    // The processes the proxy reported shown the screen of another: its pid.
    QList<uint> shown;
public Q_SLOTS:
    void x11ProcessShown(uint game, uint pid)
    {
        Q_UNUSED(game)
        shown.append(pid);
    }
    bool x11PrefixMayMatch(const QString &prefix, const QStringList &candidates)
    {
        Q_UNUSED(prefix)
        Q_UNUSED(candidates)
        return prefixMayMatch;
    }
    QVariantMap x11ConnectionPolicy(uint pid, const QStringList &candidates)
    {
        Q_UNUSED(pid)
        ++asked;
        lastCandidates = candidates;
        if (!unselected.isEmpty() && candidates.join(QLatin1Char(' ')).contains(unselected)) {
            return {{QStringLiteral("reason"), QStringLiteral("not in the list")}};
        }
        const UpscaleX11::Wire canonical;
        QByteArray timing(32, '\0');
        canonical.integer(timing, 0, 1);
        canonical.word(timing, 4, 2560);
        canonical.word(timing, 6, 1440);
        return {{QStringLiteral("width"), 2560},
                {QStringLiteral("height"), 1440},
                {QStringLiteral("timing"), timing},
                {QStringLiteral("reason"), QStringLiteral("connection display advertisement")}};
    }
};

class ProxySessionTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void oneProcessIsAskedOnce();
    void forgetsWhatAPrefixRanOnceItStops();
    void unselectedWineComponentConnectsPromptly();
    void selectedWineComponentWaitsForProgram();
    void identifiesAProgramStartedByItsUnixPath();
    void aLauncherNamesTheProgramBeforeItStarts_data();
    void aLauncherNamesTheProgramBeforeItStarts();
    void aWarmPrefixIsShownTheLaterScreen();
    void effectSwitchedOffMidSession();

private:
    // Starts a session listening at @p name, as KWin starts one. What KWin
    // keeps of the descriptors it hands over stays open until cleanup.
    std::unique_ptr<UpscaleX11::Session> startSession(const QString &name);
    // Opens a connection, sends its setup, and waits for the answer. Returns
    // the descriptor, with the root size the client was told in @p size.
    int connectClient(QSize &size);
    // The root size another program is told, from a process of its own, or
    // "failed" where it could not connect.
    QByteArray screenOfAnotherProgram();
    // Closes a connection once its relay has finished: the relay passes the
    // end of the stream back in the same step in which it finishes.
    bool disconnectClient(int client);

    QTemporaryDir m_directory;
    QByteArray m_path;
    QList<int> m_kept;
    EffectStandIn m_effect;
};
