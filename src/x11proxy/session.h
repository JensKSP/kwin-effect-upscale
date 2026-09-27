// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "registry.h"
#include "startup.h"
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QSize>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
class QSocketNotifier;
namespace UpscaleX11
{
class Relay;
struct PendingClient;
class Session : public QObject
{
public:
    explicit Session(const QString &program);
    ~Session() override;
    bool start(const QStringList &arguments);

private:
    bool listen();
    bool watchSignals();
    void acceptClient(int listener);
    void decideClient(const std::shared_ptr<PendingClient> &client);
    bool resolveCandidates(const std::shared_ptr<PendingClient> &client);
    void relayClient(int client, quint32 pid, const QSize &size, const QByteArray &timing = {}, bool answered = false);
    QTemporaryDir m_directory;
    QByteArray m_backendPath;
    Startup m_startup;
    QProcess m_server;
    QTimer m_killTimer;
    Registry m_registry;
    // The program each Wine prefix runs, found once by whichever of its
    // connections arrives while it is known and reused by all the others.
    QHash<QString, QString> m_prefixPrograms;
    // What KWin answered a process, kept while any of its connections is
    // open, with the number of those connections.
    struct Answer
    {
        QSize size;
        QByteArray timing;
        int connections = 0;
    };
    QHash<quint32, Answer> m_answers;
    QList<int> m_signalSockets;
    int m_backendListener = -1;
    int m_pendingConnections = 0;
    QSet<Relay *> m_relays;
};
}
