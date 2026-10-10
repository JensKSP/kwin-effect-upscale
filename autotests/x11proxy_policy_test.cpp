// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "policy.h"
#include "x11proxy_fixture.h"
#include <QCoreApplication>
#include <QDebug>
#include <functional>

using namespace UpscaleX11;
using namespace UpscaleX11Test;

namespace
{
void screenSizeRequest(bool little, bool selected, bool extended)
{
    Wire wire;
    wire.little = little;
    Policy policy(selected ? QSize(2560, 1440) : QSize());
    setup(policy, wire, selected);
    QByteArray query(16, '\0');
    query[0] = 98;
    wire.word(query, 2, 4);
    wire.word(query, 4, 5);
    query.replace(8, 5, "RANDR");
    check(fragmented(policy, 0, query) == query, "RandR query changed");
    QByteArray reply(32, '\0');
    reply[0] = 1;
    wire.word(reply, 2, 1);
    reply[8] = 1;
    reply[9] = char(131);
    check(fragmented(policy, 1, reply) == reply, "RandR query reply changed");
    const quint16 first = extended ? enableBigRequests(policy, wire, 2) : 2;
    const qsizetype shift = extended ? 4 : 0;
    QByteArray resize(20 + shift, '\0');
    resize[0] = char(131);
    resize[1] = 7;
    if (extended) {
        wire.integer(resize, 4, 6);
    } else {
        wire.word(resize, 2, 5);
    }
    wire.integer(resize, 4 + shift, 42);
    wire.word(resize, 8 + shift, 2560);
    wire.word(resize, 10 + shift, 1440);
    wire.integer(resize, 12 + shift, 600);
    wire.integer(resize, 16 + shift, 340);
    QByteArray noop(4, '\0');
    noop[0] = 127;
    wire.word(noop, 2, 1);
    check(fragmented(policy, 0, resize) == (selected ? noop : resize), "setting the advertised screen size reached the physical screen");
    // Forward invalid or different requests so the real server still checks
    // their resources, dimensions and physical-size arguments.
    QByteArray other = resize;
    wire.integer(other, 4 + shift, 43);
    check(fragmented(policy, 0, other) == other, "unrelated screen resize changed");
    other = resize;
    wire.word(other, 8 + shift, 3840);
    check(fragmented(policy, 0, other) == other, "different screen size changed");
    other = resize;
    wire.integer(other, 12 + shift, 0);
    check(fragmented(policy, 0, other) == other, "invalid physical size hidden");
    QByteArray geometry(8, '\0');
    geometry[0] = 14;
    wire.word(geometry, 2, 2);
    wire.integer(geometry, 4, 42);
    check(fragmented(policy, 0, geometry) == geometry, "request after resize changed");
    reply = QByteArray(32, '\0');
    reply[0] = 1;
    const quint16 following = first + 4;
    wire.word(reply, 2, following);
    wire.word(reply, 16, 3840);
    wire.word(reply, 18, 2160);
    const QByteArray result = fragmented(policy, 1, reply);
    check(wire.word(result, 2) == following && wire.word(result, 16) == (selected ? 2560 : 3840), "resize lost the following reply sequence");
}
void earlyMode(bool little)
{
    Wire canonical;
    QByteArray timing(32, '\0');
    canonical.integer(timing, 0, 123);
    canonical.word(timing, 4, 2560);
    canonical.word(timing, 6, 1440);
    canonical.integer(timing, 8, 241500000);
    Wire wire;
    wire.little = little;
    Policy policy(QSize(2560, 1440), nullptr, 0, timing);
    setup(policy, wire);
    const QByteArray name("XFree86-VidModeExtension");
    QByteArray request((8 + name.size() + 3) & ~qsizetype(3), '\0');
    request[0] = 98;
    wire.word(request, 2, static_cast<quint16>(request.size() / 4));
    wire.word(request, 4, static_cast<quint16>(name.size()));
    request.replace(8, name.size(), name);
    check(fragmented(policy, 0, request) == request, "VidMode extension request");
    QByteArray reply(32, '\0');
    reply[0] = 1;
    wire.word(reply, 2, 1);
    reply[8] = 1;
    reply[9] = char(131);
    check(fragmented(policy, 1, reply) == reply, "VidMode extension reply");
    request = QByteArray(4, '\0');
    request[0] = char(131);
    request[1] = 1;
    wire.word(request, 2, 1);
    check(fragmented(policy, 0, request) == request, "VidMode current request");
    reply.resize(52);
    wire.word(reply, 2, 2);
    wire.integer(reply, 4, 5);
    const QByteArray result = fragmented(policy, 1, reply);
    check(wire.word(result, 12) == 2560 && wire.word(result, 22) == 1440, "VidMode before RandR dimensions");
    check(wire.integer(result, 8) == 241500, "VidMode before RandR dot clock");
}
void reusedIdentity()
{
    Registry registry;
    const quint64 old = registry.insert(0x200000, 0x1fffff, 111);
    const quint64 replacement = registry.insert(0x200000, 0x1fffff, 222);
    registry.remove(0x200000, old);
    check(registry.processFor(0x200001) == 222, "old relay erased a reused resource range");
    registry.remove(0x200000, replacement);
    check(!registry.processFor(0x200001), "replacement identity retained after close");
}
void identityRecords(Policy &client, const Wire &wire, const QByteArray &query, QByteArray reply)
{
    // Include a zero-length XID record, a known PID, and an unrelated PID.
    QByteArray xid(12, '\0');
    wire.integer(xid, 0, 0x200000);
    wire.integer(xid, 4, 1);
    QByteArray unrelated = reply.mid(32);
    wire.integer(unrelated, 0, 0x400000);
    reply.insert(32, xid);
    reply += unrelated;
    wire.word(reply, 2, 3);
    wire.integer(reply, 4, 11);
    wire.integer(reply, 8, 3);
    QByteArray expected = reply;
    wire.integer(expected, 56, 4567);
    check(fragmented(client, 0, query) == query, "multiple XRes query changed");
    check(fragmented(client, 1, reply) == expected, "mixed XRes records not preserved");
    wire.word(reply, 2, 4);
    wire.integer(reply, 52, 0xffffffffU);
    check(fragmented(client, 0, query) == query, "truncated XRes query changed");
    bool rejected = false;
    try {
        fragmented(client, 1, reply);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    check(rejected, "truncated XRes payload not rejected");
}
void identity(bool little)
{
    Wire wire;
    wire.little = little;
    Registry registry;
    {
        Policy client({}, &registry, 4567);
        QByteArray request(12, '\0');
        request[0] = little ? 'l' : 'B';
        wire.word(request, 2, 11);
        check(fragmented(client, 0, request) == request, "identity setup request changed");
        QByteArray response(40, '\0');
        response[0] = 1;
        wire.word(response, 6, 8);
        wire.integer(response, 12, 0x200000);
        wire.integer(response, 16, 0x1fffff);
        check(fragmented(client, 1, response) == response, "identity setup reply changed");
        check(registry.processFor(0x200005) == 4567, "client resource not attributed");
        check(!registry.processFor(0x400000), "unknown client falsely attributed");
        QByteArray extension(20, '\0');
        extension[0] = 98;
        wire.word(extension, 2, 5);
        wire.word(extension, 4, 10);
        extension.replace(8, 10, "X-Resource");
        check(fragmented(client, 0, extension) == extension, "extension request changed");
        QByteArray reply(32, '\0');
        reply[0] = 1;
        wire.word(reply, 2, 1);
        reply[8] = 1;
        reply[9] = char(130);
        check(fragmented(client, 1, reply) == reply, "extension reply changed");
        QByteArray query(16, '\0');
        query[0] = char(130);
        query[1] = 4;
        wire.word(query, 2, 4);
        wire.integer(query, 4, 1);
        wire.integer(query, 8, 0x200005);
        wire.integer(query, 12, 2);
        check(fragmented(client, 0, query) == query, "XRes query changed");
        reply.resize(48);
        wire.word(reply, 2, 2);
        wire.integer(reply, 4, 4);
        wire.integer(reply, 8, 1);
        wire.integer(reply, 32, 0x200000);
        wire.integer(reply, 36, 2);
        // Xwayland sends this record's payload length in bytes, unlike the
        // outer reply length in four-byte units.
        wire.integer(reply, 40, 4);
        wire.integer(reply, 44, 999);
        QByteArray expected = reply;
        wire.integer(expected, 44, 4567);
        check(fragmented(client, 1, reply) == expected, "XRes original PID not restored");
        identityRecords(client, wire, query, reply);
    }
    check(!registry.processFor(0x200005), "closed client remains registered");
}
// A client in a PID namespace of its own sets _NET_WM_PID to a number of that
// namespace; the connection's process goes in its place, and in nothing else.
void processProperty(bool little)
{
    Wire wire;
    wire.little = little;
    Registry registry;
    Policy client({}, &registry, 4567);
    QByteArray request(12, '\0');
    request[0] = little ? 'l' : 'B';
    wire.word(request, 2, 11);
    check(fragmented(client, 0, request) == request, "process setup request changed");
    QByteArray response(40, '\0');
    response[0] = 1;
    wire.word(response, 6, 8);
    wire.integer(response, 12, 0x200000);
    wire.integer(response, 16, 0x1fffff);
    check(fragmented(client, 1, response) == response, "process setup reply changed");
    QByteArray change(28, '\0');
    change[0] = 18;
    wire.word(change, 2, 7);
    wire.integer(change, 4, 0x200001);
    wire.integer(change, 8, 321);
    wire.integer(change, 12, 6);
    change[16] = 32;
    wire.integer(change, 20, 1);
    wire.integer(change, 24, 2);
    check(fragmented(client, 0, change) == change, "property changed before its atom was known");
    QByteArray intern(20, '\0');
    intern[0] = 16;
    wire.word(intern, 2, 5);
    wire.word(intern, 4, 11);
    intern.replace(8, 11, "_NET_WM_PID");
    check(fragmented(client, 0, intern) == intern, "InternAtom request changed");
    QByteArray atom(32, '\0');
    atom[0] = 1;
    wire.word(atom, 2, 2);
    wire.integer(atom, 8, 321);
    check(fragmented(client, 1, atom) == atom, "InternAtom reply changed");
    QByteArray expected = change;
    wire.integer(expected, 24, 4567);
    check(fragmented(client, 0, change) == expected, "_NET_WM_PID not given the connection's process");
    QByteArray other = change;
    wire.integer(other, 8, 322);
    check(fragmented(client, 0, other) == other, "another property changed");
    QByteArray text = change;
    wire.integer(text, 12, 31);
    check(fragmented(client, 0, text) == text, "a property of another type changed");
}
void exercise(bool little)
{
    Wire wire;
    wire.little = little;
    Policy policy(QSize(2560, 1440));
    setup(policy, wire);
    QByteArray geometry(8, '\0');
    geometry[0] = 14;
    wire.word(geometry, 2, 2);
    wire.integer(geometry, 4, 42);
    check(fragmented(policy, 0, geometry) == geometry, "geometry request changed");
    QByteArray reply(32, '\0');
    reply[0] = 1;
    wire.word(reply, 2, 1);
    wire.word(reply, 16, 3840);
    wire.word(reply, 18, 2160);
    QByteArray expected = reply;
    wire.word(expected, 16, 2560);
    wire.word(expected, 18, 1440);
    check(fragmented(policy, 1, reply) == expected, "root geometry reply");
    wire.integer(geometry, 4, 43);
    check(policy.feed(0, geometry) == geometry, "window geometry request changed");
    wire.word(reply, 2, 2);
    check(policy.feed(1, reply) == reply, "unrelated window geometry changed");
    enableBigRequests(policy, wire, 3);
    QByteArray big(12, '\0');
    big[0] = 127;
    wire.integer(big, 4, 3);
    check(fragmented(policy, 0, big) == big, "unknown BIG-REQUEST changed");
    QByteArray generic(136, '\0');
    generic[0] = 35;
    wire.integer(generic, 4, 26);
    wire.word(generic, 8, 6);
    check(fragmented(policy, 1, generic) == generic, "generic input event changed");
    QByteArray event(32, '\0');
    event[0] = 6;
    check(policy.feed(1, event + event) == event + event, "coalesced input events changed");
    // An extended length of one is shorter than its own header. Xwayland
    // consumes it as four bytes and answers BadLength; ending the connection
    // over it would end a program the server keeps talking to.
    QByteArray invalid(8, '\0');
    invalid[0] = 127;
    wire.integer(invalid, 4, 1);
    const QByteArray framed = policy.feed(0, invalid);
    check(framed.startsWith(invalid.first(4)) && !policy.transparent(), "short BIG-REQUEST not framed as the server frames it");
    Policy streaming(QSize(2560, 1440));
    setup(streaming, wire);
    enableBigRequests(streaming, wire, 1);
    constexpr qsizetype total = 10 * 1024 * 1024;
    wire.integer(invalid, 4, total / 4);
    check(streaming.feed(0, invalid) == invalid, "large unknown request header changed");
    QByteArray chunk(65536, 'x');
    for (qsizetype remaining = total - 8; remaining > 0;) {
        const QByteArray next = chunk.first(std::min(remaining, chunk.size()));
        check(streaming.feed(0, next) == next, "large unknown request changed");
        remaining -= next.size();
    }
    check(!streaming.pending(0), "large request incomplete");
}
}
int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        for (const bool little : {true, false}) {
            for (const bool selected : {true, false}) {
                screenSizeRequest(little, selected, false);
                screenSizeRequest(little, selected, true);
            }
        }
        earlyMode(true);
        earlyMode(false);
        reusedIdentity();
        identity(true);
        identity(false);
        processProperty(true);
        processProperty(false);
        exercise(true);
        exercise(false);
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
    qInfo() << "PASS fragmented setup, root/window geometry, endian, BIG-REQUESTS, XGE, malformed lengths";
    return 0;
}
