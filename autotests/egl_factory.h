/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// A headless EGL display and context for the tests that draw, made through
// KWin's own classes on every KWin version this builds against.

#pragma once

#include "opengl/eglcontext.h"
#include "opengl/egldisplay.h"

#include <memory>

namespace KWin
{

// KWin's EGL factories changed shape in 6.7, a release before the effect
// callbacks did, and again on master, so the declaration is asked rather than
// the version: 6.3 and 6.6 take whether the display is owned and an EGL
// context to share, 6.7 and master a DRM device and a context of KWin's own.
// Templates, because if constexpr discards a branch only inside one.
template<typename Display>
std::unique_ptr<Display> createEglDisplay(::EGLDisplay display)
{
    if constexpr (requires { Display::create(display, nullptr); }) {
        return Display::create(display, nullptr);
    } else {
        return Display::create(display);
    }
}

template<typename Context, typename Display>
std::shared_ptr<Context> createEglContext(Display *display)
{
    if constexpr (requires { Context::create(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT); }) {
        return Context::create(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT);
    } else {
        return Context::create(display, EGL_NO_CONFIG_KHR, {});
    }
}

} // namespace KWin
