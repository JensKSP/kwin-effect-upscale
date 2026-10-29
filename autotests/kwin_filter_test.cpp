/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The scaling filters in KWin's own renderer, each kind of program once: a
// classic filter that RCAS follows, the two that sharpen themselves, and xBRZ,
// which decodes whatever the destination. The render tests compare their
// pixels with what each computes; this is where they have to survive KWin's
// buffer import, its scene and a real output.

#include "kwin_scaling_test.h"

#include "effect/effecthandler.h"
#include "scene/surfaceitem.h"
#include "scene/windowitem.h"
#include "window.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <KWayland/Client/surface.h>

#include <QScopeGuard>

void UpscaleProductionTest::drawsWithEachKindOfFilter_data()
{
    // The global layer stores the filter by number, as upscaleconfig.kcfg
    // declares it; the metrics line names it as kwinupscalerc spells it.
    QTest::addColumn<int>("filter");
    QTest::addColumn<QString>("name");
    QTest::newRow("lanczos") << 4 << QStringLiteral("Lanczos");
    QTest::newRow("gsr") << 5 << QStringLiteral("GSR");
    QTest::newRow("nis") << 6 << QStringLiteral("NIS");
    QTest::newRow("xbrz") << 7 << QStringLiteral("xBRZ");
}

void UpscaleProductionTest::drawsWithEachKindOfFilter()
{
    QFETCH(int, filter);
    QFETCH(QString, name);
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kwinrc")), QStringLiteral("Effect-upscale"));
    group.writeEntry("Filter", filter);
    group.writeEntry("Sharpening", true);
    group.writeEntry("Strength", 50);
    group.sync();
    effects->reconfigureEffect(QStringLiteral("upscale"));
    const auto restore = qScopeGuard([&group]() {
        group.deleteEntry("Filter");
        group.deleteEntry("Sharpening");
        group.deleteEntry("Strength");
        group.sync();
    });

    auto surface = Test::createSurface();
    auto fractional = Test::createFractionalScaleV1(surface.get());
    Viewport viewport(m_viewporter.get_viewport(*surface));
    viewport.set_destination(384, 216);
    auto shell = Test::createXdgToplevelSurface(surface.get(), [](Test::XdgToplevel *toplevel) {
        toplevel->set_app_id(QStringLiteral("org.kde.upscale.production"));
        toplevel->set_fullscreen(nullptr);
    });
    Window *window = Test::renderAndWaitForShown(surface.get(), pattern(QSize(384, 216)));
    QVERIFY(window);
    // Quality asks for two thirds, 1.5 times smaller: within NIS's range.
    QTRY_VERIFY(Test::waylandSync() && fractional->preferredScale() == 80);
    const QSize reduced(256, 144);
    Test::render(surface.get(), pattern(reduced));
    QVERIFY(Test::waylandSync());
    QTRY_COMPARE(window->windowItem()->surfaceItem()->bufferSize(), reduced);
    QTRY_VERIFY2(status().contains(QStringLiteral("scaling=1")) && status().contains(QStringLiteral(" filter=") + name + QLatin1Char(' ')),
                 qPrintable(status()));
    const QImage scaled = renderOutput();
    QVERIFY(!scaled.isNull());
    qInfo().noquote() << "UPSCALE_CONFORMANCE rendered" << status();

    // The same smaller buffer enlarged by KWin alone, which samples it
    // bilinearly. No filter here draws that, so the two must differ.
    effects->unloadEffect(QStringLiteral("upscale"));
    QTRY_VERIFY(Test::waylandSync() && fractional->preferredScale() == 120);
    const QImage ordinary = renderOutput();
    QCOMPARE(ordinary.size(), scaled.size());
    QVERIFY2(ordinary != scaled, qPrintable(name + QStringLiteral(" output must differ from ordinary KWin enlargement")));
    QCOMPARE(window->windowItem()->surfaceItem()->bufferSize(), reduced);
}
