/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// What a window drawn over its output does with what its picture covers:
// the pointer's cursor over a decoration the picture hides.

#include "kwin_scaling_test.h"

#include "input.h"
#include "pointer_input.h"
#include "wayland/seat.h"
#include "wayland_server.h"
#include "window.h"

#include <KWayland/Client/output.h>
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
