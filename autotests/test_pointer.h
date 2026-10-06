/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "core/inputdevice.h"

#include <QPointF>
#include <QString>

#include <chrono>

namespace KWin
{

// The virtual backend has no input devices, and without a pointer among them
// the seat offers clients no pointer at all, so nothing a test injects would
// ever reach an X window. This device exists so that one does. A test moves it
// by writing a position to a file in the private runtime directory, which
// the driver polls: the test process cannot reach the compositor's input, and
// what then arrives at the X client is the end of the whole path - focus,
// seat, Xwayland - rather than any one piece of it.
class TestPointer : public InputDevice
{
public:
    QString name() const override
    {
        return QStringLiteral("upscale test pointer");
    }
    bool isEnabled() const override
    {
        return true;
    }
    void setEnabled(bool) override
    {
    }
    bool isKeyboard() const override
    {
        return false;
    }
    bool isPointer() const override
    {
        return true;
    }
    bool isTouchpad() const override
    {
        return false;
    }
    bool isTouch() const override
    {
        return false;
    }
    bool isTabletTool() const override
    {
        return false;
    }
    bool isTabletPad() const override
    {
        return false;
    }
    bool isTabletModeSwitch() const override
    {
        return false;
    }
    bool isLidSwitch() const override
    {
        return false;
    }

    void move(const QPointF &position)
    {
        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
        Q_EMIT pointerMotionAbsolute(position, now, this);
        // Clients speaking wl_pointer version 5 or later, Xwayland among
        // them, act on a motion only when the frame that closes it arrives.
        Q_EMIT pointerFrame(this);
    }

    // A motion by @p delta, as a mouse reports one, with no position of its own.
    void moveBy(const QPointF &delta)
    {
        const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
        Q_EMIT pointerMotion(delta, delta, now, this);
        Q_EMIT pointerFrame(this);
    }

    // A left click there. KWin takes evdev's button codes on every platform,
    // and BTN_LEFT is 0x110 among them.
    void click(const QPointF &position)
    {
        move(position);
        for (const PointerButtonState state : {PointerButtonState::Pressed, PointerButtonState::Released}) {
            const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch());
            Q_EMIT pointerButtonChanged(0x110, state, now, this);
            Q_EMIT pointerFrame(this);
        }
    }
};

} // namespace KWin
