/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
#include "config-kwin.h"
#include "eligibility.h"

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QTimer>

#include <memory>

#if KWIN_BUILD_X11
#include "x11eventfilter.h"
#endif

namespace KWin
{
class EffectWindow;
class SurfaceInterface;
class UpscaleX11Input;
class Window;
class X11Window;

/** A live, profile-selected request; the client still owns its renderer. */
class UpscaleX11Resolution : public QObject
#if KWIN_BUILD_X11
    ,
                             private X11EventFilter
#endif
{
public:
    UpscaleX11Resolution();
    ~UpscaleX11Resolution() override;
    void reconfigure(bool enabled, ResolutionPreset preset, int percentage);
    QSize requested(const Window *window) const;
    QString failure(const Window *window) const;
    /** Who is enlarging this window's buffer to the output right now. */
    UpscaleX11Presentation presentation(const Window *window) const;
    /**
     * The window the effect is presenting under @p position, as the factor
     * its pointer coordinates have to be scaled by, with its surface and the
     * origin they are taken from. Exactly one by one when there is none, and
     * when Xwayland presents the window there, because then its surface is
     * the frame's size and nothing needs scaling. The ratio of the surface to
     * the frame is the ratio the scaler enlarges by, which is what keeps the
     * picture and the pointer agreed.
     */
    QPointF presentedUnder(const QPointF &position, SurfaceInterface **surface, QPointF *origin) const;

#if KWIN_BUILD_X11
    /** One live request: the window it went to and what it asked for. */
    struct Request
    {
        QPointer<X11Window> window;
        QString key;
        QPoint position;
        QSize size;
        bool primaryOnly = false;
        // Whether this effect sized the surface item to the frame, because
        // the client supplied the requested buffer without establishing an
        // emulated mode. Decided once, when that buffer first arrives.
        bool presentedByEffect = false;
    };
#endif

private:
    using QObject::event;
#if KWIN_BUILD_X11
    struct Attempt
    {
        QPointer<X11Window> window;
        int count = 0;
    };
    bool event(xcb_generic_event_t *generic) override;
    bool fullscreenRequest(X11Window *window, xcb_client_message_event_t *message);
    void watch(EffectWindow *window);
    void forget(X11Window *window);
    void expireState();
    void schedule(X11Window *window);
    void apply(X11Window *window);
    void present(X11Window *window);
    Request requestFor(X11Window *window) const;
    static QString keyFor(const Window *window);
    bool begin(const Request &request);
    void validate(const QString &key, int generation, int revision);
    static QString unmetCondition(const Request &request);
    bool retry(const QString &key, int generation);
    void refuse(const QString &key, const QString &reason);
    void restore(X11Window *window);
    void restoreAll();

    QHash<X11Window *, Request> m_requests;
    QHash<QString, QSize> m_requested;
    QHash<QString, QString> m_failures;
    QHash<QString, Attempt> m_attempts;
    QHash<QString, int> m_retries;
    QHash<QString, int> m_validation;
    QSet<X11Window *> m_watched;
    QSet<X11Window *> m_scheduled;
    QSet<X11Window *> m_waitingForBuffer;
    QTimer m_expiration;
    std::unique_ptr<UpscaleX11Input> m_input;
    bool m_enabled = false;
    bool m_restoring = false;
    ResolutionPreset m_preset = ResolutionPreset::Automatic;
    int m_percentage = 100;
    int m_generation = 0;
    int m_nextValidation = 0;
    xcb_atom_t m_stateAtom = XCB_ATOM_NONE;
    xcb_atom_t m_fullscreenAtom = XCB_ATOM_NONE;
#endif
};
}
