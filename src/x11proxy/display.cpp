// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "display.h"
#include <QDebug>

namespace UpscaleX11
{

DisplayReplies::DisplayReplies(Wire &wire, QSize size, QByteArray timing)
    : m_wire(wire)
    , m_size(size)
    , m_initialTiming(std::move(timing))
{
    if (size.width() <= 0 || size.height() <= 0 || size.width() > 65535 || size.height() > 65535) {
        throw std::runtime_error("Invalid virtual dimensions");
    }
}

void DisplayReplies::dimensions(QByteArray &bytes, qsizetype offset, bool wide) const
{
    if (wide) {
        m_wire.integer(bytes, offset, m_size.width());
        m_wire.integer(bytes, offset + 4, m_size.height());
    } else {
        m_wire.word(bytes, offset, static_cast<quint16>(m_size.width()));
        m_wire.word(bytes, offset + 2, static_cast<quint16>(m_size.height()));
    }
}

void DisplayReplies::setup(QByteArray &bytes)
{
    if (Wire::byte(bytes, 28) != 1) {
        disable("Display policy requires one X11 screen");
        return;
    }
    const qsizetype offset = 40 + ((m_wire.word(bytes, 24) + 3) & ~3) + (Wire::byte(bytes, 29) * 8);
    m_root = m_wire.integer(bytes, offset);
    qInfo() << "POLICY setup root=" << m_root << "native=" << m_wire.word(bytes, offset + 20)
            << m_wire.word(bytes, offset + 22) << "virtual=" << m_size;
    dimensions(bytes, offset + 20);
    if (!m_initialTiming.isEmpty()) {
        // The effect supplies the real target timing before setup. A client
        // may ask VidMode or CRTC state before it enumerates RandR modes.
        const Wire canonical;
        m_currentTiming = m_initialTiming;
        for (const int field : {0, 8, 28}) {
            m_wire.integer(m_currentTiming, field, canonical.integer(m_initialTiming, field));
        }
        for (const int field : {4, 6, 12, 14, 16, 18, 20, 22, 24, 26}) {
            m_wire.word(m_currentTiming, field, canonical.word(m_initialTiming, field));
        }
        m_currentMode = m_wire.integer(m_currentTiming, 0);
        m_modes.insert(m_currentMode);
    }
}

void DisplayReplies::disable(const char *reason)
{
    m_enabled = false;
    m_modes.clear();
    m_currentMode = 0;
    m_currentTiming.clear();
    qInfo() << "Upscale X11 display advertisement withdrawn:" << reason;
}

quint16 DisplayReplies::nativeSizeIndex(quint16 index) const
{
    const auto found = m_sizes.constFind(index);
    if (found == m_sizes.cend()) {
        throw std::runtime_error("Unknown virtual legacy mode index");
    }
    return *found;
}

void DisplayReplies::request(quint8 operation, QByteArray &bytes, qsizetype shift)
{
    // RRSetScreenConfig is 24 bytes, or 20 from a client that predates rates
    // (randr/rrscreen.c). One of any other size is the server's to refuse, so
    // it is relayed unchanged rather than rewritten past its end.
    if (operation == 2 && (m_enabled || !m_sizes.isEmpty()) && (bytes.size() == 20 + shift || bytes.size() == 24 + shift)) {
        m_wire.word(bytes, 16 + shift, nativeSizeIndex(m_wire.word(bytes, 16 + shift)));
    } else if (m_enabled && operation == 7 && bytes.size() == 20 + shift
               && m_wire.integer(bytes, 4 + shift) == m_root
               && m_wire.word(bytes, 8 + shift) == m_size.width()
               && m_wire.word(bytes, 10 + shift) == m_size.height()
               && m_wire.integer(bytes, 12 + shift) && m_wire.integer(bytes, 16 + shift)) {
        // This connection already has the requested screen size. Xwayland's
        // per-client CRTC emulation leaves the physical CRTC unchanged, so a
        // real RRSetScreenSize would fail its physical containment check.
        // One NoOperation preserves request sequence numbers without changing
        // the shared desktop. Other requests retain the server's validation.
        bytes = QByteArray(4, '\0');
        bytes[0] = 127;
        m_wire.word(bytes, 2, 1);
        const QByteArray key("RANDR:SetScreenSize");
        if (!m_reported.contains(key)) {
            m_reported.insert(key);
            qInfo() << "POLICY request" << key << "already satisfied by connection display" << m_size;
        }
    }
}

QByteArray DisplayReplies::reply(const QByteArray &kind, quint32 operation, QByteArray bytes)
{
    if (!m_enabled) {
        // A client may still use its last virtual legacy index until it
        // refreshes the size table. The new native table ends that mapping.
        if (kind == "RANDR" && operation == 5) {
            m_sizes.clear();
        }
        return bytes;
    }
    const QByteArray before = bytes;
    if (kind == "geometry" && operation == m_root) {
        dimensions(bytes, 16);
    } else if (kind == "RANDR") {
        bytes = randr(operation, std::move(bytes));
    } else if (kind == "XINERAMA") {
        if (operation == 3) {
            dimensions(bytes, 8, true);
        } else if (operation == 5) {
            const quint32 monitors = m_wire.integer(bytes, 8);
            if (monitors > static_cast<quint64>((bytes.size() - 32) / 8)) {
                throw std::runtime_error("Truncated Xinerama monitor list");
            }
            if (monitors != 1) {
                disable("Display policy requires one Xinerama monitor");
                return bytes;
            }
            dimensions(bytes, 36);
        }
    } else if (kind == "XFree86-VidModeExtension") {
        bytes = vidmode(operation, std::move(bytes));
    }
    const QByteArray key = kind + ':' + QByteArray::number(operation);
    if (bytes != before && !m_reported.contains(key)) {
        m_reported.insert(key);
        qInfo() << "POLICY reply" << key;
    }
    return bytes;
}

QByteArray DisplayReplies::randr(quint32 operation, QByteArray bytes)
{
    if (operation == 8 || operation == 25) {
        return resources(bytes);
    }
    if (operation == 9 && !m_modes.isEmpty()) {
        return outputInfo(bytes);
    }
    if (operation == 20 && m_wire.integer(bytes, 20)) {
        dimensions(bytes, 16);
        if (m_currentMode) {
            m_wire.integer(bytes, 20, m_currentMode);
        }
    } else if (operation == 5) {
        return screenInfo(bytes);
    } else if (operation == 42) {
        const quint32 monitors = m_wire.integer(bytes, 12);
        const quint32 outputs = m_wire.integer(bytes, 16);
        const quint64 length = (quint64(monitors) * 24) + (quint64(outputs) * 4);
        if (length > static_cast<quint64>(bytes.size() - 32)) {
            throw std::runtime_error("Truncated RandR monitor list");
        }
        // One monitor can span several outputs, which the resources reply
        // refuses; a client that asks for monitors first is refused the same.
        if (monitors != 1 || outputs != 1) {
            disable("Display policy requires one RandR monitor on one output");
            return bytes;
        }
        dimensions(bytes, 44);
    }
    return bytes;
}

QByteArray DisplayReplies::outputInfo(const QByteArray &bytes)
{
    const quint16 crtcCount = m_wire.word(bytes, 26);
    const quint16 modeCount = m_wire.word(bytes, 28);
    const quint16 cloneCount = m_wire.word(bytes, 32);
    const quint16 nameLength = m_wire.word(bytes, 34);
    const qsizetype start = 36 + (crtcCount * 4);
    QList<quint32> modes;
    for (quint16 index = 0; index < modeCount; ++index) {
        const quint32 mode = m_wire.integer(bytes, start + (qsizetype(index) * 4));
        if (m_modes.contains(mode)) {
            modes.append(mode);
        }
    }
    // The backend no longer offers the requested size under any mode this
    // connection was told about. Asked before the resources, which withdraw
    // the policy in the same case, the output would otherwise have no mode.
    if (modes.isEmpty()) {
        disable("Requested size absent from output modes");
        return bytes;
    }
    if (modes.removeOne(m_currentMode)) {
        modes.prepend(m_currentMode);
    }
    QByteArray result = Wire::slice(bytes, 0, start);
    for (const quint32 mode : modes) {
        const qsizetype offset = result.size();
        result.append(QByteArray(4, '\0'));
        m_wire.integer(result, offset, mode);
    }
    result += Wire::slice(bytes, start + (qsizetype(modeCount) * 4), (qsizetype(cloneCount) * 4) + nameLength);
    m_wire.word(result, 28, static_cast<quint16>(modes.size()));
    m_wire.word(result, 30, modes.contains(m_currentMode) ? 1 : 0);
    return result;
}

QByteArray DisplayReplies::resources(const QByteArray &bytes)
{
    const quint16 crtcCount = m_wire.word(bytes, 16);
    const quint16 outputCount = m_wire.word(bytes, 18);
    const quint16 modeCount = m_wire.word(bytes, 20);
    Wire::require(bytes, 32, ((qsizetype(crtcCount) + outputCount) * 4) + (qsizetype(modeCount) * 32) + m_wire.word(bytes, 22));
    if (outputCount != 1) {
        disable("Display policy requires one RandR output");
        return bytes;
    }
    const qsizetype start = 32 + ((crtcCount + outputCount) * 4);
    qsizetype nameOffset = start + (qsizetype(modeCount) * 32);
    QByteArray modes;
    QByteArray names;
    m_modes.clear();
    m_currentMode = 0;
    m_currentTiming.clear();
    quint16 kept = 0;
    for (quint16 index = 0; index < modeCount; ++index) {
        const QByteArray mode = Wire::slice(bytes, start + (qsizetype(index) * 32), 32);
        const quint16 nameLength = m_wire.word(mode, 26);
        const QByteArray name = Wire::slice(bytes, nameOffset, nameLength);
        nameOffset += nameLength;
        const QSize size(m_wire.word(mode, 4), m_wire.word(mode, 6));
        if (size == m_size) {
            modes += mode;
            names += name;
            m_modes.insert(m_wire.integer(mode, 0));
            ++kept;
            if (size == m_size && !m_currentMode) {
                m_currentMode = m_wire.integer(mode, 0);
                m_currentTiming = mode;
            }
        }
    }
    if (!m_currentMode) {
        disable("Requested size absent from backend modes");
        return bytes;
    }
    QByteArray result = Wire::slice(bytes, 0, start) + modes + names;
    m_wire.word(result, 20, kept);
    m_wire.word(result, 22, static_cast<quint16>(names.size()));
    return result;
}

QByteArray DisplayReplies::changed(quint16 sequence, quint16 selected) const
{
    if (!m_enabled || randrEvent < 0) {
        return {};
    }
    QByteArray events;
    // RRScreenChangeNotifyMask and RRCrtcChangeNotifyMask (randr/randrstr.h).
    if (selected & 1) {
        QByteArray event(32, '\0');
        event[0] = static_cast<char>(randrEvent);
        event[1] = 1; // RR_Rotate_0
        m_wire.word(event, 2, sequence);
        m_wire.integer(event, 12, m_root);
        m_wire.integer(event, 16, m_root);
        dimensions(event, 24);
        events += event;
    }
    if (selected & 2) {
        QByteArray event(32, '\0');
        event[0] = static_cast<char>(randrEvent + 1);
        event[1] = 0; // CrtcChange
        m_wire.word(event, 2, sequence);
        m_wire.integer(event, 8, m_root);
        m_wire.integer(event, 16, m_currentMode);
        m_wire.word(event, 20, 1);
        dimensions(event, 28);
        events += event;
    }
    return events;
}

void DisplayReplies::event(QByteArray &bytes)
{
    if (!m_enabled) {
        return;
    }
    const quint8 kind = Wire::byte(bytes, 0) & 127;
    if (kind == randrEvent) {
        dimensions(bytes, 24);
    } else if (kind == 22 && m_wire.integer(bytes, 8) == m_root) {
        dimensions(bytes, 20);
    }
}

} // namespace UpscaleX11
