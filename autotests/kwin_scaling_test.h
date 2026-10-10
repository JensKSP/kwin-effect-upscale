/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// KWin's production test with this effect loaded, built in KWin's own test
// framework by tools/prepare-kwin-tests.py: scaling in kwin_scaling_test.cpp,
// what a window drawn over its output does with input and stacking in
// kwin_presentation_test.cpp.

#pragma once

#include "kwin_wayland_test.h"
#include "qwayland-tablet-unstable-v2.h"
#include "qwayland-viewporter.h"

#include <KWayland/Client/event_queue.h>
#include <KWayland/Client/registry.h>

#include <QImage>

#include <memory>

using namespace KWin;

class Viewport : public QtWayland::wp_viewport
{
public:
    explicit Viewport(::wp_viewport *viewport)
        : QtWayland::wp_viewport(viewport)
    {
    }
    ~Viewport() override
    {
        destroy();
    }
};

class UpscaleProductionTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();
    void reducesAndScales_data();
    void reducesAndScales();
    void advertisedModeProducesSmallerBuffer();
    void unpluggedOutputIsPassedOver();
    void unsupportedBufferFallsBack_data();
    void unsupportedBufferFallsBack();
    void fitsAnotherAspectRatio();
    void ignoredRequestIsRestored();
    void windowedClientIsUnchanged();
    void nativeBufferBypassesScaling();
    void aHiddenDecorationKeepsTheGamesCursor();
    void presentsThePictureOfItsOnlySubsurface_data();
    void presentsThePictureOfItsOnlySubsurface();
    void mapsAPenOntoThePicture();
    void keepsAPenWholeAcrossABar();

private:
    void configure(bool enabled, const QString &method = QStringLiteral("Auto"));
    QString status() const;
    QImage renderOutput() const;
    static QImage pattern(const QSize &size);
    std::unique_ptr<KWayland::Client::EventQueue> m_queue;
    std::unique_ptr<KWayland::Client::Registry> m_registry;
    QtWayland::wp_viewporter m_viewporter;
    QtWayland::zwp_tablet_manager_v2 m_tablets;
};
