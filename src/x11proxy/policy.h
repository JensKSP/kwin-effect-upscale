// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "display.h"
#include "framer.h"
#include "registry.h"
#include <memory>
#include <optional>

namespace UpscaleX11
{

class Policy
{
public:
    // The largest frame held whole to be read. Larger ones are relayed as
    // they arrive. Coalesced frames are drained before buffering more input.
    static constexpr qsizetype s_bufferLimit = Framer::s_bufferLimit;
    explicit Policy(QSize size = {}, Registry *registry = nullptr, quint32 pid = 0, const QByteArray &timing = {});
    ~Policy();
    QByteArray feed(std::size_t side, const QByteArray &bytes);
    bool transparent() const
    {
        return m_framer.transparent();
    }
    bool pending(std::size_t side) const
    {
        return m_framer.pending(side);
    }
    /**
     * Show the client a screen of @p size from now on, as a program of its
     * Wine prefix was answered after this connection was: one prefix is one
     * Wine desktop, and its programs have to agree on its screen.
     *
     * Returns the events that tell the client so, where they can be sent at
     * once, between two of the server's messages; otherwise they follow the
     * message the server is in the middle of. Nothing is returned before the
     * setup reply, which then shows the new size itself, and nothing for a
     * connection this relay no longer reads.
     */
    QByteArray changeDisplay(QSize size, const QByteArray &timing);
    /** Whether changeDisplay() would show the client another screen than it sees. */
    bool changesDisplay(QSize size) const
    {
        return size.isValid() && size != m_size && !m_framer.transparent();
    }

private:
    struct Request
    {
        QByteArray kind;
        quint32 operation = 0;
        QByteArray extension;
    };
    QByteArray frame(std::size_t side, QByteArray message, Framer::FrameKind kind);
    void request(QByteArray &bytes);
    void selectInput(const QByteArray &bytes, qsizetype shift);
    void learnExtension(const QByteArray &name, const QByteArray &reply);
    QByteArray response(QByteArray bytes);
    void setupReply(QByteArray &bytes);
    void resourceReply(QByteArray &bytes);
    bool streamable(std::size_t side, const QByteArray &bytes) const;
    Wire m_wire;
    Framer m_framer{m_wire};
    std::unique_ptr<DisplayReplies> m_display;
    Registry *m_registry;
    quint32 m_pid;
    std::optional<quint32> m_base;
    quint64 m_registration = 0;
    quint64 m_sequence = 0;
    QHash<quint16, Request> m_requests;
    QHash<quint8, QByteArray> m_extensions;
    // What a view made after setup learns the root and the screen from: the
    // server's own setup reply, before any rewriting.
    QByteArray m_setupReply;
    QSize m_size;
    // The sequence of the last message relayed to the client, which is the
    // last it read: an event given a lower one would read as a wraparound.
    quint16 m_lastSequence = 0;
    int m_randrEvent = -1;
    // The RandR events each window was selected for (RRSelectInput).
    QHash<quint32, quint16> m_randrSelections;
    // Events held until the server's message now in progress has ended.
    QByteArray m_deferred;
};

} // namespace UpscaleX11
