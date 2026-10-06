/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What a window drawn over its output does with what its picture covers - the
// pointer's cursor over a decoration the picture hides - and a window whose
// picture is in a subsurface of its own, as Wine's Wayland driver draws one.

#include "kwin_scaling_test.h"

#include "input.h"
#include "pointer_input.h"
#include "wayland/seat.h"
#include "wayland_server.h"
#include "window.h"

#include <KWayland/Client/output.h>
#include <KWayland/Client/subsurface.h>
#include <KWayland/Client/surface.h>

// A decorated window under a game drawn over its output, whose decoration the
// picture hides. KWin finds that decoration under the pointer before any
// filter runs, and while a decoration has the pointer KWin shows that
// decoration's cursor (CursorImage::reevaluteSource()): an arrow over the title
// bar, a resize cursor at the border, where the player sees the game. Without
// one it shows the cursor of the surface the seat's pointer is on.
void UpscaleProductionTest::aHiddenDecorationKeepsTheGamesCursor()
{
    quint32 time = 0;
    Test::pointerMotion(QPointF(382, 214), ++time);
    std::unique_ptr<KWayland::Client::Surface> belowSurface = Test::createSurface();
    std::unique_ptr<Test::XdgToplevel> belowShell = Test::createXdgToplevelSurface(belowSurface.get(), Test::CreationSetup::CreateOnly);
    std::unique_ptr<Test::XdgToplevelDecorationV1> decoration = Test::createXdgToplevelDecorationV1(belowShell.get());
    QSignalSpy configured(belowShell->xdgSurface(), &Test::XdgSurface::configureRequested);
    decoration->set_mode(Test::XdgToplevelDecorationV1::mode_server_side);
    belowSurface->commit(KWayland::Client::Surface::CommitFlag::None);
    QVERIFY(configured.wait());
    belowShell->xdgSurface()->ack_configure(configured.last().at(0).value<quint32>());
    Window *below = Test::renderAndWaitForShown(belowSurface.get(), QSize(160, 60), Qt::blue);
    QVERIFY(below);
    QVERIFY(below->isDecorated());
    below->move(QPointF(8, 8));

    // The game: a plain window of the size it was told at connection, which
    // the effect draws over the whole output and stacks above the other.
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    std::unique_ptr<KWayland::Client::Surface> gameSurface = Test::createSurface();
    std::unique_ptr<Test::XdgToplevel> gameShell = Test::createXdgToplevelSurface(gameSurface.get());
    Window *game = Test::renderAndWaitForShown(gameSurface.get(), pattern(QSize(256, 144)));
    QVERIFY(game);
    game->move(QPointF(120, 64));
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    Test::pointerMotion(game->frameGeometry().center(), ++time);
    QTRY_COMPARE(waylandServer()->seat()->focusedPointerSurface(), game->surface());

    // Over the hidden title bar, and over the hidden corner where KWin would
    // offer a resize: the picture is the game's, and so is the cursor.
    const QRectF frame = below->frameGeometry();
    for (const QPointF &hidden : {QPointF(frame.left() + 40, frame.top() + 4), frame.topLeft() + QPointF(1, 1)}) {
        QVERIFY2(!game->frameGeometry().contains(hidden), "the point has to lie beside the game, over the other window");
        Test::pointerMotion(hidden, ++time);
        QCOMPARE(input()->pointer()->hover(), below);
        QCOMPARE(waylandServer()->seat()->focusedPointerSurface(), game->surface());
        QVERIFY2(!input()->pointer()->decoration(),
                 qPrintable(QStringLiteral("at %1, %2 the hidden decoration has the pointer, and KWin shows its cursor").arg(hidden.x()).arg(hidden.y())));
    }
}

// Wine's Wayland driver draws a game into a subsurface covering its window,
// whose own buffer shows nothing of it, and rounds the subsurface's buffer up
// to a height that is a multiple of 128, showing the told size of it through a
// viewport. The picture is the subsurface's, as much of it as the viewport
// shows: 256 × 144 here, of a 256 × 160 buffer, in a window of the told size.
void UpscaleProductionTest::presentsThePictureOfItsOnlySubsurface()
{
    QTRY_VERIFY(Test::waylandSync() && Test::waylandOutputs().first()->pixelSize() == QSize(256, 144));
    std::unique_ptr<KWayland::Client::Surface> surface = Test::createSurface();
    std::unique_ptr<KWayland::Client::Surface> content = Test::createSurface();
    std::unique_ptr<KWayland::Client::SubSurface> subsurface = Test::createSubSurface(content.get(), surface.get());
    QVERIFY(subsurface);
    Viewport viewport(m_viewporter.get_viewport(*content));
    viewport.set_source(wl_fixed_from_int(0), wl_fixed_from_int(0), wl_fixed_from_int(256), wl_fixed_from_int(144));
    viewport.set_destination(256, 144);
    Test::render(content.get(), pattern(QSize(256, 160)));
    std::unique_ptr<Test::XdgToplevel> shell = Test::createXdgToplevelSurface(surface.get());
    QImage black(QSize(256, 144), QImage::Format_RGB32);
    black.fill(Qt::black);
    Window *window = Test::renderAndWaitForShown(surface.get(), black);
    QVERIFY(window);
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")), qPrintable(status()));
    QVERIFY2(status().contains(QStringLiteral("supplied=256x144")), qPrintable(status()));
    // The picture is the subsurface's, not the window's black buffer: the
    // pattern is blue where it lies right of its diagonal, as in the middle.
    const QImage output = renderOutput();
    QVERIFY(!output.isNull());
    QVERIFY2(qBlue(output.pixel(output.width() / 2, output.height() / 2)) > 100,
             qPrintable(QString::number(output.pixel(output.width() / 2, output.height() / 2), 16)));
}
