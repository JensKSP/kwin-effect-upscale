// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "display.h"
#include "x11proxy_fixture.h"
#include <QCoreApplication>
#include <QDebug>
using namespace UpscaleX11;
using namespace UpscaleX11Test;

namespace
{

QByteArray resources(const Wire &wire, quint16 outputs, bool requestedMode)
{
    const qsizetype start = 36 + (outputs * 4);
    QByteArray reply(start + 32, '\0');
    reply[0] = 1;
    wire.integer(reply, 4, static_cast<quint32>((reply.size() - 32) / 4));
    wire.word(reply, 16, 1);
    wire.word(reply, 18, outputs);
    wire.word(reply, 20, 1);
    wire.integer(reply, start, 123);
    wire.word(reply, start + 4, requestedMode ? 2560 : 3840);
    wire.word(reply, start + 6, requestedMode ? 1440 : 2160);
    return reply;
}

QByteArray screenInfo(const Wire &wire, bool requestedMode)
{
    QByteArray reply(52, '\0');
    reply[0] = 1;
    wire.integer(reply, 4, 5);
    wire.word(reply, 20, 2);
    wire.word(reply, 28, 2);
    wire.word(reply, 32, 3840);
    wire.word(reply, 34, 2160);
    wire.word(reply, 40, requestedMode ? 2560 : 1920);
    wire.word(reply, 42, requestedMode ? 1440 : 1080);
    return reply;
}

// One RandR monitor listing the given number of outputs, at the native size.
QByteArray monitors(const Wire &wire, quint16 outputs)
{
    QByteArray reply(32 + 24 + (outputs * 4), '\0');
    reply[0] = 1;
    wire.integer(reply, 4, static_cast<quint32>((reply.size() - 32) / 4));
    wire.integer(reply, 12, 1);
    wire.integer(reply, 16, outputs);
    wire.word(reply, 38, outputs);
    wire.word(reply, 44, 3840);
    wire.word(reply, 46, 2160);
    return reply;
}

// One output on one CRTC, offering the given modes.
QByteArray outputInfo(const Wire &wire, const QList<quint32> &modes)
{
    QByteArray reply(40 + (modes.size() * 4), '\0');
    reply[0] = 1;
    wire.integer(reply, 4, static_cast<quint32>((reply.size() - 32) / 4));
    wire.word(reply, 26, 1);
    wire.word(reply, 28, static_cast<quint16>(modes.size()));
    wire.word(reply, 30, 1);
    wire.integer(reply, 36, 7);
    for (qsizetype index = 0; index < modes.size(); ++index) {
        wire.integer(reply, 40 + (index * 4), modes.at(index));
    }
    return reply;
}

QByteArray rootGeometry(const Wire &wire)
{
    QByteArray geometry(32, '\0');
    geometry[0] = 1;
    wire.word(geometry, 16, 3840);
    wire.word(geometry, 18, 2160);
    return geometry;
}

void setUpRoot(DisplayReplies &display, const Wire &wire)
{
    QByteArray setupReply(80, '\0');
    setupReply[28] = 1;
    wire.integer(setupReply, 40, 42);
    display.setup(setupReply);
}

void nativeAfterChange(bool little, bool hotplug)
{
    Wire wire;
    wire.little = little;
    DisplayReplies display(wire, QSize(2560, 1440));
    QByteArray setupReply(80, '\0');
    setupReply[28] = 1;
    wire.integer(setupReply, 40, 42);
    display.setup(setupReply);
    display.randrEvent = 90;
    const QByteArray initial = resources(wire, 1, true);
    check(display.reply("RANDR", 25, initial) == initial, "initial mode changed");
    const QByteArray legacy = display.reply("RANDR", 5, screenInfo(wire, true));
    check(wire.word(legacy, 20) == 1 && display.nativeSizeIndex(0) == 1, "legacy mode fixture not mapped");

    const QByteArray changed = resources(wire, hotplug ? 2 : 1, hotplug);
    check(display.reply("RANDR", 25, changed) == changed, "changed resources not preserved");
    QByteArray geometry(32, '\0');
    geometry[0] = 1;
    wire.word(geometry, 16, 3840);
    wire.word(geometry, 18, 2160);
    check(display.reply("geometry", 42, geometry) == geometry, "native geometry rewritten after fallback");
    // Returning to one output does not revive a connection's old policy.
    check(display.reply("RANDR", 25, resources(wire, 1, false)) == resources(wire, 1, false), "display policy revived");
    for (const quint8 kind : {quint8(90), quint8(22)}) {
        QByteArray event(32, '\0');
        event[0] = static_cast<char>(kind);
        wire.integer(event, 8, 42);
        const QByteArray before = event;
        display.event(event);
        check(event == before, "event rewritten after fallback");
    }
    QByteArray request(20, '\0');
    wire.integer(request, 4, 42);
    wire.word(request, 8, 2560);
    wire.word(request, 10, 1440);
    wire.integer(request, 12, 600);
    wire.integer(request, 16, 340);
    const QByteArray before = request;
    display.request(7, request, 0);
    check(request == before, "screen size request still suppressed");
    QByteArray config(24, '\0');
    display.request(2, config, 0);
    check(wire.word(config, 16) == 1, "cached virtual legacy index lost");
    const QByteArray native = screenInfo(wire, false);
    check(display.reply("RANDR", 5, native) == native, "native legacy table changed");
    wire.word(config, 16, 0);
    display.request(2, config, 0);
    check(wire.word(config, 16) == 0, "native legacy index still remapped");
}

// Display fallback does not turn the whole protocol transparent: XRes still
// names the original process rather than the relay's PID.
void identityAfterChange(bool little)
{
    Wire wire;
    wire.little = little;
    Registry registry;
    Policy policy(QSize(2560, 1440), &registry, 4567);
    setup(policy, wire);
    registry.insert(0x200000, 0x1fffff, 4567);
    quint16 sequence = learnExtension(policy, wire, "RANDR", 131, 1);
    QByteArray query(8, '\0');
    query[0] = char(131);
    query[1] = 25;
    wire.word(query, 2, 2);
    wire.integer(query, 4, 42);
    check(fragmented(policy, 0, query) == query, "resource query changed");
    QByteArray changed = resources(wire, 2, true);
    wire.word(changed, 2, sequence++);
    check(fragmented(policy, 1, changed) == changed, "fallback changed resources");
    check(!policy.transparent(), "display fallback abandoned protocol identity");
    sequence = learnExtension(policy, wire, "X-Resource", 130, sequence);
    query = QByteArray(16, '\0');
    query[0] = char(130);
    query[1] = 4;
    wire.word(query, 2, 4);
    wire.integer(query, 4, 1);
    wire.integer(query, 8, 0x200005);
    wire.integer(query, 12, 2);
    check(fragmented(policy, 0, query) == query, "XRes query changed");
    QByteArray reply(48, '\0');
    reply[0] = 1;
    wire.word(reply, 2, sequence);
    wire.integer(reply, 4, 4);
    wire.integer(reply, 8, 1);
    wire.integer(reply, 32, 0x200000);
    wire.integer(reply, 36, 2);
    wire.integer(reply, 40, 4);
    wire.integer(reply, 44, 999);
    const QByteArray restored = fragmented(policy, 1, reply);
    check(wire.integer(restored, 44) == 4567, "fallback stopped restoring PID");
}

void legacyModeDisappears(bool little)
{
    Wire wire;
    wire.little = little;
    DisplayReplies display(wire, QSize(2560, 1440));
    display.reply("RANDR", 5, screenInfo(wire, true));
    const QByteArray changed = screenInfo(wire, false);
    check(display.reply("RANDR", 5, changed) == changed, "missing legacy mode closed connection");
    QByteArray config(24, '\0');
    display.request(2, config, 0);
    check(wire.word(config, 16) == 0, "new native legacy table kept old index mapping");
    DisplayReplies malformed(wire, QSize(2560, 1440));
    QByteArray truncated = resources(wire, 2, true);
    truncated.chop(4);
    bool rejected = false;
    try {
        malformed.reply("RANDR", 25, truncated);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected, "truncated topology reply mistaken for supported fallback");
}

// A monitor spanning two outputs is refused as the resources refuse two
// outputs, for a client that asks for the monitors first.
void monitorOnSeveralOutputs(bool little)
{
    Wire wire;
    wire.little = little;
    DisplayReplies single(wire, QSize(2560, 1440));
    setUpRoot(single, wire);
    const QByteArray one = single.reply("RANDR", 42, monitors(wire, 1));
    check(wire.word(one, 44) == 2560 && wire.word(one, 46) == 1440, "monitor on one output not rewritten");

    DisplayReplies spanning(wire, QSize(2560, 1440));
    setUpRoot(spanning, wire);
    const QByteArray two = monitors(wire, 2);
    check(spanning.reply("RANDR", 42, two) == two, "monitor spanning two outputs rewritten");
    check(spanning.reply("geometry", 42, rootGeometry(wire)) == rootGeometry(wire), "policy kept after a monitor spanning two outputs");
}

// The backend replaced the requested mode and the client reads the output
// before the resources: the output keeps its native modes and the policy ends.
void outputModeDisappears(bool little)
{
    Wire wire;
    wire.little = little;
    DisplayReplies display(wire, QSize(2560, 1440));
    setUpRoot(display, wire);
    display.reply("RANDR", 25, resources(wire, 1, true));
    const QByteArray offered = display.reply("RANDR", 9, outputInfo(wire, {456, 123}));
    check(wire.word(offered, 28) == 1 && wire.integer(offered, 40) == 123, "output modes not narrowed to the requested one");

    const QByteArray replaced = outputInfo(wire, {456});
    check(display.reply("RANDR", 9, replaced) == replaced, "output left without its native modes");
    check(display.reply("geometry", 42, rootGeometry(wire)) == rootGeometry(wire), "policy kept after its mode disappeared");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        for (const bool little : {true, false}) {
            nativeAfterChange(little, true);
            nativeAfterChange(little, false);
            identityAfterChange(little);
            legacyModeDisappears(little);
            monitorOnSeveralOutputs(little);
            outputModeDisappears(little);
        }
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
    return 0;
}
