// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "identity.h"
#include "relay.h"
#include "session.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDeadlineTimer>
#include <QDebug>
#include <QVariantMap>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
namespace UpscaleX11
{
// Wine's own components come up before the program the prefix was started for,
// and name only themselves. A prefix that is starting is worth waiting for,
// because bringing one up takes seconds and its screen cannot be decided
// without the program; an ordinary client is never held this long.
constexpr int prefixDecisionMilliseconds = 10000;
// Finding the program means reading every readable process, so the wait is
// coarse: a prefix takes seconds to come up and a quarter second of it is not
// felt, while a tighter interval would scan far more often for nothing.
constexpr int prefixRetryMilliseconds = 250;
struct PendingClient
{
    int descriptor = -1;
    quint32 pid = 0;
    QDeadlineTimer deadline{500};
    bool deferred = false;
    bool waiting = false;
    ProgramIdentity identity;
    QStringList candidates;
    ~PendingClient()
    {
        if (descriptor >= 0) {
            close(descriptor);
        }
    }
};

void Session::acceptClient(int listener)
{
    const int client = accept(listener, nullptr, nullptr);
    if (client < 0) {
        return;
    }
    // Bound outstanding decisions and active relays together. A client that
    // never sends setup must not exhaust this process's file descriptors.
    if (m_pendingConnections + m_relays.size() >= 512 || !socketFlags(client)) {
        close(client);
        return;
    }
    ++m_pendingConnections;
    const quint32 pid = peerProcess(client);
    if (!pid) {
        relayClient(client, pid, {});
        return;
    }
    // A process is asked about once. A connection it opens while an earlier
    // one is still open gets that one's answer: one program sees one screen,
    // and a connection opened midway is not held for a round trip to KWin,
    // which took 7.5 ms and at worst the full 500 ms deadline (measured
    // 2026-09-27). The X Test Suite's grab and focus cases open one between
    // two steps and lost their race with KWin placing the window in between.
    if (const auto known = m_answers.constFind(pid); known != m_answers.cend()) {
        relayClient(client, pid, known->size, known->timing, true, known->prefix);
        return;
    }
    auto pending = std::make_shared<PendingClient>();
    pending->descriptor = client;
    pending->pid = pid;
    pending->identity = upscaleProgramIdentity(pid);
    if (pending->identity.isWine()) {
        pending->deadline = QDeadlineTimer(prefixDecisionMilliseconds);
    }
    if (pending->identity.isWine() && pending->identity.component) {
        checkPrefix(pending);
        return;
    }
    decideClient(pending);
}

void Session::checkPrefix(const std::shared_ptr<PendingClient> &client)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/org/kde/KWin/Effect/Upscale1"), QStringLiteral("org.kde.KWin.Effect.Upscale1"),
                                                          QStringLiteral("x11PrefixMayMatch"));
    message << client->identity.prefix << client->identity.candidates();
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 500), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, client]() {
        const QDBusPendingReply<bool> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError() || !reply.value()) {
            relayClient(std::exchange(client->descriptor, -1), client->pid, {}, {}, !reply.isError(), client->identity.prefix);
            return;
        }
        client->deadline = QDeadlineTimer(prefixDecisionMilliseconds);
        decideClient(client);
    });
}

bool Session::resolveCandidates(const std::shared_ptr<PendingClient> &client)
{
    const ProgramIdentity &identity = client->identity;
    client->candidates = identity.candidates();
    if (!identity.isWine()) {
        return true;
    }
    // One prefix is one Wine server, one registry and one Windows desktop, so
    // every connection it makes is answered for the same program. A connection
    // that is the program itself settles the prefix; the rest reuse that.
    if (!identity.component && !identity.program.isEmpty()) {
        m_prefixPrograms.insert(identity.prefix, identity.program);
    }
    QString program = m_prefixPrograms.value(identity.prefix);
    if (program.isEmpty()) {
        program = upscalePrefixProgram(identity.prefix);
        if (program.isEmpty()) {
            return false;
        }
        qInfo() << "Upscale X11 prefix" << identity.prefix << "runs" << program;
        m_prefixPrograms.insert(identity.prefix, program);
    }
    // The prefix's own program is named the same way the connecting process
    // would have been, so one pattern matches whichever of them arrives first.
    const QString named = upscaleRuntimeIdentity(u"wine", identity.prefix, program);
    client->candidates = identity.candidates();
    if (!named.isEmpty() && !client->candidates.contains(named)) {
        client->candidates.prepend(named);
    }
    return true;
}

void Session::decideClient(const std::shared_ptr<PendingClient> &client)
{
    if (!resolveCandidates(client) && client->deadline.remainingTime() > prefixRetryMilliseconds) {
        if (!client->waiting) {
            qInfo() << "Upscale X11 connection pid=" << client->pid << "waiting for the program of prefix"
                    << client->identity.prefix;
            client->waiting = true;
        }
        QTimer::singleShot(prefixRetryMilliseconds, this, [this, client]() {
            decideClient(client);
        });
        return;
    }
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/org/kde/KWin/Effect/Upscale1"), QStringLiteral("org.kde.KWin.Effect.Upscale1"),
                                                          QStringLiteral("x11ConnectionPolicy"));
    message << client->pid << client->candidates;
    // No client byte is read before the decision. D-Bus is asynchronous: the
    // WM channel remains responsive while KWin decides, including at startup.
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, int(std::max<qint64>(1, client->deadline.remainingTime()))), this);
    // The pending owner closes the socket if shutdown cancels either the
    // asynchronous reply or a retry waiting for KWin's X11 mode list.
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, client]() {
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            qInfo() << "Upscale X11 connection pid=" << client->pid << "unchanged: policy unavailable" << reply.error().name();
            relayClient(std::exchange(client->descriptor, -1), client->pid, {}, {}, false, client->identity.prefix);
            return;
        }
        const QVariantMap policy = reply.value();
        if (policy.value(QStringLiteral("retry")).toBool() && client->deadline.remainingTime() > 20) {
            if (!client->deferred) {
                qInfo() << "Upscale X11 connection pid=" << client->pid << "waiting for display policy readiness";
                client->deferred = true;
            }
            QTimer::singleShot(20, this, [this, client]() {
                decideClient(client);
            });
            return;
        }
        const QByteArray timing = policy.value(QStringLiteral("timing")).toByteArray();
        QSize size(policy.value(QStringLiteral("width")).toInt(), policy.value(QStringLiteral("height")).toInt());
        if (timing.size() != 32 || size.width() < 1 || size.height() < 1 || size.width() > 65535 || size.height() > 65535) {
            size = {};
        }
        // The names are reported with the answer: a pattern that matches none
        // of them leaves the program at the native size and says nothing by
        // itself, so the names it could have matched have to be visible.
        qInfo() << "Upscale X11 connection pid=" << client->pid << "profile=" << policy.value(QStringLiteral("profile"))
                << "size=" << size << "reason=" << policy.value(QStringLiteral("reason"))
                << "names=" << client->candidates;
        relayClient(std::exchange(client->descriptor, -1), client->pid, size, timing, true, client->identity.prefix);
    });
    // The watcher belongs to this session, its parent, and deletes itself once
    // the reply is in. The static analyzer, entering here from a retry, does
    // not model a QObject parent and reports the watcher as leaked at this
    // brace.
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
void Session::relayClient(int client, quint32 pid, const QSize &answeredSize, const QByteArray &answeredTiming, bool answered,
                          const QString &prefix)
{
    --m_pendingConnections;
    // A connection of a prefix that shows its game's screen is shown that,
    // whatever it was answered itself: one prefix is one screen, and a program
    // of it at the full size would disagree with the rest, as Jens decided on
    // 2026-09-29.
    QSize size = answeredSize;
    QByteArray timing = answeredTiming;
    quint32 game = 0;
    if (const auto shown = m_prefixShown.constFind(prefix); !prefix.isEmpty() && !answeredSize.isValid() && shown != m_prefixShown.cend()) {
        size = shown->size;
        timing = shown->timing;
        game = shown->game;
        answered = true;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, m_backendPath.constData(), static_cast<std::size_t>(m_backendPath.size() + 1));
    const int backend = socket(AF_UNIX, SOCK_STREAM, 0);
    if (backend < 0 || !socketFlags(backend)
        || ::connect(backend, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        close(client);
        if (backend >= 0) {
            close(backend);
        }
        qWarning() << "Upscale X11 backend connection failed";
        return;
    }
    auto *relay = new Relay(client, backend, this, {.size = size, .timing = timing, .registry = &m_registry, .pid = pid});
    m_relays.insert(relay);
    if (answered && pid) {
        Answer &answer = m_answers[pid];
        answer.size = size;
        answer.timing = timing;
        answer.prefix = prefix;
        ++answer.connections;
    }
    if (!prefix.isEmpty()) {
        ++m_prefixConnections[prefix];
        if (!game) {
            showPrefix(prefix, size, timing, pid);
        }
        m_prefixRelays[prefix].insert(relay);
    }
    if (game && pid && pid != game) {
        tellShown(game, pid);
    }
    connect(relay, &QObject::destroyed, this, [this, relay, pid, answered, prefix]() {
        forgetRelay(relay, pid, answered, prefix);
    });
}

// One prefix is one Wine desktop with one screen. A program selected in a
// prefix that already runs - a launcher first, or Wine's own tools - has the
// prefix's earlier connections shown its screen too, and told it changed, so
// that Wine reads its displays again (decided by Jens on 2026-09-29).
// Otherwise the program is answered while Wine already knows the screen at
// full size.
void Session::showPrefix(const QString &prefix, const QSize &size, const QByteArray &timing, quint32 game)
{
    if (!size.isValid()) {
        return;
    }
    m_prefixShown.insert(prefix, {size, timing, game});
    QSet<quint32> processes;
    const QSet<Relay *> earlier = m_prefixRelays.value(prefix);
    for (const Relay *other : earlier) {
        if (other->pid() && other->pid() != game) {
            processes.insert(other->pid());
        }
    }
    if (processes.isEmpty()) {
        switchPrefix(prefix, size, timing);
        return;
    }
    // The effect first: it makes a fullscreen window of these processes the
    // game's size and presents it, and has to have done so before Wine hears
    // of the smaller screen, or KWin answers Wine's request for that size with
    // the output's and Wine grows the window by the difference.
    // Each answer, or its half second, counts; the switch follows the last.
    auto remaining = std::make_shared<qsizetype>(processes.size());
    const auto proceed = [this, prefix, size, timing, remaining]() {
        if (--*remaining == 0) {
            switchPrefix(prefix, size, timing);
        }
    };
    // The watchers belong to this session and delete themselves. The static
    // analyzer, which does not know a parent owns them, sees each one leak at
    // the next turn of the loop and at the end; see decideClient().
    for (const quint32 process : std::as_const(processes)) { // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(shownMessage(game, process), 500), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, proceed]() {
            watcher->deleteLater();
            proceed();
        });
    }
} // NOLINT(clang-analyzer-cplusplus.NewDeleteLeaks)

void Session::switchPrefix(const QString &prefix, const QSize &size, const QByteArray &timing)
{
    int changed = 0;
    const QSet<Relay *> earlier = m_prefixRelays.value(prefix);
    for (Relay *other : earlier) {
        if (!other->changeDisplay(size, timing)) {
            continue;
        }
        ++changed;
        if (const auto known = m_answers.find(other->pid()); known != m_answers.end()) {
            known->size = size;
            known->timing = timing;
        }
    }
    if (changed) {
        qInfo() << "Upscale X11 prefix" << prefix << "now shows" << changed << "earlier connections" << size;
    }
}

void Session::forgetRelay(Relay *relay, quint32 pid, bool answered, const QString &prefix)
{
    m_relays.remove(relay);
    if (const auto relays = m_prefixRelays.find(prefix); relays != m_prefixRelays.end()) {
        relays->remove(relay);
        if (relays->isEmpty()) {
            m_prefixRelays.erase(relays);
        }
    }
    const auto answer = m_answers.find(pid);
    if (answered && answer != m_answers.end() && --answer->connections == 0) {
        m_answers.erase(answer);
    }
    const auto open = m_prefixConnections.find(prefix);
    if (!prefix.isEmpty() && open != m_prefixConnections.end() && --*open == 0) {
        m_prefixConnections.erase(open);
        m_prefixPrograms.remove(prefix);
        m_prefixShown.remove(prefix);
    }
}

QDBusMessage Session::shownMessage(quint32 game, quint32 pid)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/org/kde/KWin/Effect/Upscale1"),
                                                          QStringLiteral("org.kde.KWin.Effect.Upscale1"), QStringLiteral("x11ProcessShown"));
    message << game << pid;
    return message;
}

void Session::tellShown(quint32 game, quint32 pid)
{
    QDBusConnection::sessionBus().call(shownMessage(game, pid), QDBus::NoBlock);
}
}
