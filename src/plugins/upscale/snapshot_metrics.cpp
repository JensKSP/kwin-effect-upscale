/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "snapshot.h"

#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdlib>

namespace KWin
{

// Within a hundred and twentieth of the output and a pixel: the fractional
// scale travels in 120ths, so a client that honours it exactly can land that
// far from the size computed here, and that is the game taking the request,
// not declining it.
bool upscaleDrawsTheChosenSize(const UpscaleSnapshot &snapshot)
{
    const QSize chosen(snapshot.desired.width, snapshot.desired.height);
    if (chosen.isEmpty() || snapshot.supplied.isEmpty() || snapshot.destination.isEmpty()) {
        return true;
    }
    const auto near = [](int drawn, int wished, int whole) {
        return std::abs(drawn - wished) <= whole / 120 + 1;
    };
    return near(snapshot.supplied.width(), chosen.width(), snapshot.destination.width())
        && near(snapshot.supplied.height(), chosen.height(), snapshot.destination.height());
}

// A line for programs rather than for people. Every key and every value here
// is written with QStringLiteral and never translated, because a measurement
// harness comparing two runs to a decimal place cannot depend on the language
// the session happens to run in. The prose above says the same things for a
// reader; this says them for a script.
//
// A key is left out rather than given a placeholder when nothing was measured,
// so a missing key means "not measured" and never zero.
QString upscaleMetrics(const UpscaleSnapshot &snapshot)
{
    QStringList fields;
    const auto append = [&fields](QLatin1String key, const QString &value) {
        fields.append(key + QLatin1Char('=') + value);
    };
    const auto number = [&append](QLatin1String key, double value, int digits) {
        if (value >= 0) {
            append(key, QString::number(value, 'f', digits));
        }
    };
    const auto size = [&append](QLatin1String key, const QSize &value) {
        if (!value.isEmpty()) {
            append(key, QString::number(value.width()) + QLatin1Char('x') + QString::number(value.height()));
        }
    };
    // Every number below is written with QString::number on purpose, which
    // formats as C does regardless of the session's language. This line is
    // read by machines - tools/frame_metrics.py parses it - and a decimal
    // comma or a grouped thousand would break them. The figures a person
    // reads are localised; this line is not a figure a person reads.
    number(QLatin1String("presented"), snapshot.presentedRate, 2);
    number(QLatin1String("low"), snapshot.presentedLow, 2);
    number(QLatin1String("p99"), snapshot.presentedPercentile, 3);
    number(QLatin1String("worst"), snapshot.presentedWorst, 3);
    if (snapshot.presentedFrames > 0) {
        append(QLatin1String("frames"), QString::number(snapshot.presentedFrames));
    }
    number(QLatin1String("client"), snapshot.clientUpdates, 2);
    number(QLatin1String("repaints"), snapshot.repaints, 2);
    if (snapshot.interval > 0) {
        number(QLatin1String("interval"), snapshot.interval, 3);
    }
    size(QLatin1String("supplied"), snapshot.supplied);
    size(QLatin1String("destination"), snapshot.destination);
    // Which window this describes, so that a harness can tell the game it
    // launched from whatever else the effect happened to be following. The
    // fields are separated by spaces, and a window class is not always one
    // word, so its spaces become hyphens rather than new fields.
    if (!snapshot.application.isEmpty()) {
        append(QLatin1String("window"), QString(snapshot.application).replace(QLatin1Char(' '), QLatin1Char('-')));
    }
    append(QLatin1String("scaling"), QString::number(snapshot.scaling ? 1 : 0));
    // Whether this frame cost the output its direct scanout. A comparison that
    // did not record it is comparing composition against scanout without
    // saying so, and the difference between those is part of what is measured.
    append(QLatin1String("scanout"), QLatin1String(snapshot.blocksScanout ? "blocked" : "direct"));
    append(QLatin1String("selected"), QString::number(snapshot.selected ? 1 : 0));
    switch (snapshot.windowSystem) {
    case UpscaleWindowSystem::Wayland:
        append(QLatin1String("windowsystem"), QStringLiteral("wayland"));
        break;
    case UpscaleWindowSystem::X11:
        append(QLatin1String("windowsystem"), QStringLiteral("x11"));
        break;
    case UpscaleWindowSystem::Unknown:
        break;
    }
    switch (snapshot.bufferKind) {
    case UpscaleBufferKind::Gpu:
        append(QLatin1String("buffer"), QStringLiteral("gpu"));
        break;
    case UpscaleBufferKind::SharedMemory:
        append(QLatin1String("buffer"), QStringLiteral("memory"));
        break;
    case UpscaleBufferKind::Unknown:
        break;
    }
    return QStringLiteral("metrics: ") + fields.join(QLatin1Char(' '));
}

} // namespace KWin
