/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwin_scaling_test.h"

#include "game_entry.h"

#include "core/output.h"
#include "core/renderloop.h"
#include "effect/effecthandler.h"
#include "opengl/gltexture.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "scene/workspacescene.h"
#include "wayland_server.h"
#include "workspace.h"

#include <KConfigGroup>
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/output.h>
#include <KWayland/Client/surface.h>

// The global profile is All games, and this test's clients are a game's: a
// desktop entry in the Game category starts this program.
void UpscaleProductionTest::initTestCase()
{
    QVERIFY(upscaleDeclareGame(QCoreApplication::applicationFilePath()));
    QVERIFY(waylandServer()->init(QStringLiteral("wayland_upscale_production")));
    Test::setOutputConfig({QRect(0, 0, 384, 216)});
    kwinApp()->start();
    QVERIFY(effects->isOpenGLCompositing());
    QVERIFY(effects->isEffectLoaded(QStringLiteral("upscale")));
}

void UpscaleProductionTest::cleanupTestCase()
{
    QVERIFY(upscaleDeclareGame(QCoreApplication::applicationFilePath(), false));
}

void UpscaleProductionTest::configure(bool enabled, const QString &method)
{
    const auto config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup group(config, QStringLiteral("Effect-upscale"));
    group.writeEntry("UnlistedApplications", enabled);
    group.writeEntry("Resolution", 2); // Quality: two thirds of each dimension.
    group.writeEntry("MethodWaylandFullScreen", method);
    group.writeEntry("MethodWaylandBorderless", method);
    group.writeEntry("MinimumPixels", 0);
    group.writeEntry("Osd", false);
    group.sync();
    effects->reconfigureEffect(QStringLiteral("upscale"));
}

void UpscaleProductionTest::init()
{
    if (!effects->isEffectLoaded(QStringLiteral("upscale"))) {
        QVERIFY(effects->loadEffect(QStringLiteral("upscale")));
    }
    configure(true);
    Test::setOutputConfig({QRect(0, 0, 384, 216)});
    // A seat and server-side decorations for the cases of
    // kwin_presentation_test.cpp; no other case uses them.
    QVERIFY(Test::setupWaylandConnection(Test::AdditionalWaylandInterface::FractionalScaleManagerV1
                                         | Test::AdditionalWaylandInterface::Seat
                                         | Test::AdditionalWaylandInterface::XdgDecorationV1));
    m_queue = std::make_unique<KWayland::Client::EventQueue>();
    m_queue->setup(Test::waylandConnection());
    m_registry = std::make_unique<KWayland::Client::Registry>();
    m_registry->setEventQueue(m_queue.get());
    connect(m_registry.get(), &KWayland::Client::Registry::interfaceAnnounced, this,
            [this](const QByteArray &name, quint32 id, quint32 version) {
        if (name == QByteArrayLiteral("wp_viewporter")) {
            m_viewporter.init(*m_registry, id, version);
        } else if (name == QByteArrayLiteral("zwp_tablet_manager_v2")) {
            m_tablets.init(*m_registry, id, version);
        }
    });
    QSignalSpy announced(m_registry.get(), &KWayland::Client::Registry::interfacesAnnounced);
    m_registry->create(Test::waylandConnection());
    m_registry->setup();
    // Like the upstream connection helper, run a blocking event loop so the
    // compositor flushes its queued registry announcements before we bind.
    QVERIFY(announced.wait());
    QVERIFY(m_viewporter.isInitialized());
}

void UpscaleProductionTest::cleanup()
{
    if (m_viewporter.isInitialized()) {
        m_viewporter.destroy();
    }
    if (m_tablets.isInitialized()) {
        m_tablets.destroy();
    }
    m_registry.reset();
    m_queue.reset();
    Test::destroyWaylandConnection();
    QTRY_VERIFY(workspace()->windows().isEmpty());
}

QString UpscaleProductionTest::status() const
{
    return effects->supportInformation(QStringLiteral("upscale"));
}

QImage UpscaleProductionTest::pattern(const QSize &size)
{
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            image.setPixel(x, y, qRgb((x % 5) * 50, (y % 5) * 50, x > y ? 200 : 20));
        }
    }
    return image;
}

QImage UpscaleProductionTest::renderOutput() const
{
    // A presentation already in flight can satisfy the first wait. Request
    // another full frame so the image belongs to the state being asserted.
    for (int frame = 0; frame < 2; ++frame) {
        QSignalSpy presented(workspace()->outputs().first()->renderLoop(), &RenderLoop::framePresented);
        effects->addRepaintFull();
        if (!presented.wait()) {
            return {};
        }
    }
    effects->makeOpenGLContextCurrent();
    const auto [texture, colors] = effects->scene()->textureForOutput(workspace()->outputs().first());
    return texture ? texture->toImage() : QImage();
}

void UpscaleProductionTest::reducesAndScales_data()
{
    QTest::addColumn<double>("scale");
    QTest::addColumn<bool>("fullscreen");
    for (double scale : {1.0, 1.5, 3.0}) {
        for (bool fullscreen : {false, true}) {
            QTest::addRow("scale-%g-%s", scale, fullscreen ? "fullscreen" : "borderless") << scale << fullscreen;
        }
    }
}

void UpscaleProductionTest::reducesAndScales()
{
    QFETCH(double, scale);
    QFETCH(bool, fullscreen);
    const QSize logical(int(384 / scale), int(216 / scale));
    Test::OutputInfo output;
    output.geometry = QRect(QPoint(), logical);
    output.scale = scale;
    Test::setOutputConfig({output});
    auto surface = Test::createSurface();
    auto fractional = Test::createFractionalScaleV1(surface.get());
    Viewport viewport(m_viewporter.get_viewport(*surface));
    viewport.set_destination(logical.width(), logical.height());
    auto shell = Test::createXdgToplevelSurface(surface.get(), [fullscreen](Test::XdgToplevel *toplevel) {
        toplevel->set_app_id(QStringLiteral("org.kde.upscale.production"));
        if (fullscreen) {
            toplevel->set_fullscreen(nullptr);
        }
    });
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(QSize(384, 216)));
    QVERIFY(window);
    QCOMPARE(window->isFullScreen(), fullscreen);
    QTRY_VERIFY(Test::waylandSync() && fractional->preferredScale() == qRound(scale * 80));
    // Derive the next buffer from the received request, as a cooperating
    // client does. Inspect the committed buffer independently on the server.
    const QSize reduced = logical * (fractional->preferredScale() / 120.0);
    QCOMPARE(reduced, QSize(256, 144));
    Test::render(surface.get(), pattern(reduced));
    QVERIFY(Test::waylandSync());
    QTRY_COMPARE(window->windowItem()->surfaceItem()->bufferSize(), reduced);
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    QCOMPARE(workspace()->outputs().first()->pixelSize(), QSize(384, 216));
    QCOMPARE(window->frameGeometry().size(), QSizeF(logical));
    const QImage scaled = renderOutput();
    QVERIFY(!scaled.isNull());
    qInfo().noquote() << "UPSCALE_CONFORMANCE rendered" << status();

    // Render the identical smaller client buffer through ordinary KWin, then
    // compare pixels. A status string alone cannot establish shader output.
    effects->unloadEffect(QStringLiteral("upscale"));
    QTRY_VERIFY(Test::waylandSync() && fractional->preferredScale() == qRound(scale * 120));
    const QImage ordinary = renderOutput();
    QCOMPARE(ordinary.size(), scaled.size());
    QVERIFY2(ordinary != scaled, "FSR output must differ from ordinary KWin enlargement");
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), reduced);
    QCOMPARE(workspace()->outputs().first()->pixelSize(), QSize(384, 216));
}

void UpscaleProductionTest::ignoredRequestIsRestored()
{
    Test::setOutputConfig({QRect(0, 0, 384, 216)});
    auto surface = Test::createSurface();
    auto fractional = Test::createFractionalScaleV1(surface.get());
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_fullscreen(nullptr);
    });
    // Keep acknowledging configures while ignoring their suggested size, as
    // a live client does. Unacknowledged configures can hold later commits.
    connect(shell->xdgSurface(), &Test::XdgSurface::configureRequested, shell.get(), [xdg = shell->xdgSurface()](quint32 serial) {
        xdg->ack_configure(serial);
    });
    QSignalSpy configured(shell.get(), &Test::XdgToplevel::configureRequested);
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(QSize(384, 216)));
    QVERIFY(window);
    QTRY_VERIFY(Test::waylandSync() && fractional->preferredScale() == 80);
    configured.clear();
    // Ignore the scale, then the smaller fullscreen configure. Restoring the
    // scale starts Auto's second request; only restoring its geometry ends it.
    // Wait for those protocol states rather than counting presented frames:
    // the effect judges a frame before the presentation signal is emitted.
    QTRY_VERIFY_WITH_TIMEOUT((Test::render(surface.get(), pattern(QSize(384, 216))), Test::waylandSync() && !configured.isEmpty() && configured.last().first().toSize() == QSize(256, 144)), 30000);
    QCOMPARE(fractional->preferredScale(), 120);
    QTRY_VERIFY_WITH_TIMEOUT((Test::render(surface.get(), pattern(QSize(384, 216))), Test::waylandSync() && !status().contains(QStringLiteral("as its Wayland window size")) && window->moveResizeGeometry().size() == QSizeF(384, 216)), 30000);
    QCOMPARE(fractional->preferredScale(), 120);
    QVERIFY(!status().contains(QStringLiteral("scaling=1")));
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), QSize(384, 216));
    configured.clear();
    QSignalSpy presented(workspace()->outputs().first()->renderLoop(), &RenderLoop::framePresented);
    QTRY_VERIFY_WITH_TIMEOUT((Test::render(surface.get(), pattern(QSize(384, 216))), Test::waylandSync() && presented.count() >= 35), 30000);
    QVERIFY(Test::waylandSync());
    QVERIFY2(configured.isEmpty(), "An ignored resize must not be retried every thirty frames");
}

void UpscaleProductionTest::advertisedModeProducesSmallerBuffer()
{
    // The client's initial output mode was received while binding the
    // connection in init(), before any window or surface request existed.
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    const QSize advertised = Test::waylandOutputs().first()->pixelSize();
    auto surface = Test::createSurface();
    Viewport viewport(m_viewporter.get_viewport(*surface));
    viewport.set_destination(384, 216);
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_fullscreen(nullptr);
    });
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(advertised));
    QVERIFY(window);
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), advertised);
    QCOMPARE(workspace()->outputs().first()->pixelSize(), QSize(384, 216));
    qInfo().noquote() << "UPSCALE_CONFORMANCE rendered" << status();
    configure(false);
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(384, 216));
    QVERIFY(!status().contains(QStringLiteral("scaling=1")));
}

// An output unplugged while its program is recorded as told a smaller mode on
// it. Test::setOutputConfig recreates every output, so the call below unplugs
// the one the client was told about at connection and destroys KWin's backend
// output with it, which switching an output off, as the nested session does,
// never does. Giving the mode back must pass over it, and the outputs plugged
// in meanwhile are told about like any other.
void UpscaleProductionTest::unpluggedOutputIsPassedOver()
{
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    Test::setOutputConfig({QRect(0, 0, 384, 216), QRect(384, 0, 384, 216)});
    const auto told = [](const QSize &size) {
        const auto outputs = Test::waylandOutputs();
        return outputs.size() == 2 && outputs.at(0)->pixelSize() == size && outputs.at(1)->pixelSize() == size;
    };
    QTRY_VERIFY(Test::waylandSync() && told(QSize(256, 144)));
    configure(false);
    QTRY_VERIFY(Test::waylandSync() && told(QSize(384, 216)));
    QVERIFY(effects->isEffectLoaded(QStringLiteral("upscale")));
}

void UpscaleProductionTest::unsupportedBufferFallsBack_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<bool>("transparent");
    QTest::newRow("native") << QSize(384, 216) << false;
    QTest::newRow("below-half") << QSize(96, 54) << false;
    QTest::newRow("transparent") << QSize(256, 144) << true;
}

void UpscaleProductionTest::unsupportedBufferFallsBack()
{
    QFETCH(QSize, size);
    QFETCH(bool, transparent);
    configure(true, QStringLiteral("Off"));
    auto surface = Test::createSurface();
    Viewport viewport(m_viewporter.get_viewport(*surface));
    viewport.set_destination(384, 216);
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_fullscreen(nullptr);
    });
    QImage image = pattern(size);
    if (transparent) {
        image = QImage(size, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(100, 20, 10, 128));
    }
    Window *window = Test::renderAndWaitForShown(surface.get(), image);
    QVERIFY(window);
    const QImage withEffect = renderOutput();
    QVERIFY2(status().contains(QStringLiteral("scaling=0")), qPrintable(status()));
    QVERIFY(!withEffect.isNull());
    effects->unloadEffect(QStringLiteral("upscale"));
    const QImage ordinary = renderOutput();
    const QString images = qEnvironmentVariable("UPSCALE_CONFORMANCE_IMAGES");
    if (ordinary != withEffect && !images.isEmpty()) {
        const QString prefix = images + QLatin1Char('/') + QString::fromLatin1(QTest::currentDataTag());
        withEffect.save(prefix + QStringLiteral("-effect.png"));
        ordinary.save(prefix + QStringLiteral("-ordinary.png"));
    }
    QCOMPARE(ordinary, withEffect);
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), size);
}

void UpscaleProductionTest::fitsAnotherAspectRatio()
{
    configure(true, QStringLiteral("Off"));
    auto surface = Test::createSurface();
    Viewport viewport(m_viewporter.get_viewport(*surface));
    viewport.set_destination(384, 216);
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_fullscreen(nullptr);
    });
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(QSize(256, 150)));
    QVERIFY(window);
    const QImage fitted = renderOutput();
    QVERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    QCOMPARE(fitted.size(), QSize(384, 216));
    QCOMPARE(fitted.pixelColor(0, 108), QColor(Qt::black));
    QCOMPARE(fitted.pixelColor(383, 108), QColor(Qt::black));
    QVERIFY(fitted.pixelColor(192, 108) != QColor(Qt::black));
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), QSize(256, 150));
}

void UpscaleProductionTest::windowedClientIsUnchanged()
{
    Test::setOutputConfig({QRect(0, 0, 384, 216)});
    auto surface = Test::createSurface();
    auto fractional = Test::createFractionalScaleV1(surface.get());
    auto shell = Test::createXdgToplevelSurface(surface.get());
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(QSize(160, 90)));
    QVERIFY(window);
    // A frame presented with the window in it, committed again until one
    // follows: the effect has looked at the window, and whatever it asked of
    // the client has arrived after the sync.
    QSignalSpy rendered(workspace()->outputs().first()->renderLoop(), &RenderLoop::framePresented);
    QTRY_VERIFY((Test::render(surface.get(), pattern(QSize(160, 90))), !rendered.isEmpty()));
    QVERIFY(Test::waylandSync());
    QCOMPARE(fractional->preferredScale(), 120);
    QCOMPARE(window->frameGeometry().size(), QSizeF(160, 90));
    QVERIFY(!status().contains(QStringLiteral("scaling=1")));
}

void UpscaleProductionTest::nativeBufferBypassesScaling()
{
    configure(true, QStringLiteral("Off"));
    Test::setOutputConfig({QRect(0, 0, 384, 216)});
    auto surface = Test::createSurface();
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_fullscreen(nullptr);
    });
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(QSize(384, 216)));
    QVERIFY(window);
    // A frame presented with the window in it, committed again until one
    // follows: the effect has looked at the window, and whatever it asked of
    // the client has arrived after the sync.
    QSignalSpy rendered(workspace()->outputs().first()->renderLoop(), &RenderLoop::framePresented);
    QTRY_VERIFY((Test::render(surface.get(), pattern(QSize(384, 216))), !rendered.isEmpty()));
    QVERIFY(Test::waylandSync());
    QVERIFY2(status().contains(QStringLiteral("scaling=0")), qPrintable(status()));
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), QSize(384, 216));
}

WAYLANDTEST_MAIN(UpscaleProductionTest)
