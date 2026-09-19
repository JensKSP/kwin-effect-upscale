/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
#include "config-kwin.h"

#include <QHash>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QSize>

#if KWIN_BUILD_X11
#include "x11eventfilter.h"
#endif

namespace KWin
{
class EffectWindow;
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

private:
    using QObject::event;
#if KWIN_BUILD_X11
    struct Request
    {
        QPointer<X11Window> window;
        QString key;
        QPoint position;
        QSize size;
        bool primaryOnly = false;
    };

    bool event(xcb_generic_event_t *generic) override;
    bool fullscreenRequest(X11Window *window, xcb_client_message_event_t *message);
    void watch(EffectWindow *window);
    void schedule(X11Window *window);
    void apply(X11Window *window);
    Request requestFor(X11Window *window) const;
    static QString keyFor(const Window *window);
    bool begin(const Request &request);
    void validate(const QString &key, int generation, int revision);
    bool retry(const QString &key, int generation);
    void refuse(const QString &key, const QString &reason);
    void restore(X11Window *window);
    void restoreAll();

    QHash<X11Window *, Request> m_requests;
    QHash<QString, QSize> m_requested;
    QHash<QString, QString> m_failures;
    QHash<QString, int> m_attempts;
    QHash<QString, int> m_retries;
    QHash<QString, int> m_validation;
    QSet<X11Window *> m_watched;
    QSet<X11Window *> m_scheduled;
    QSet<X11Window *> m_waitingForBuffer;
    bool m_enabled = false;
    bool m_restoring = false;
    ResolutionPreset m_preset = ResolutionPreset::Automatic;
    int m_percentage = 100;
    int m_generation = 0;
    xcb_atom_t m_stateAtom = XCB_ATOM_NONE;
    xcb_atom_t m_fullscreenAtom = XCB_ATOM_NONE;
#endif
};
}
