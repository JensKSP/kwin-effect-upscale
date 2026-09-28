// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#include "policy.h"
#include "x11proxy_fixture.h"
#include <QCoreApplication>
#include <QDebug>
using namespace UpscaleX11;
using namespace UpscaleX11Test;

// The relay sits in front of every X11 program in the session, so what a
// client sends is not something it may refuse on the server's behalf. These
// are the requests the X Test Suite's protocol cases send on purpose, and the
// framing a hostile client could use to make the relay read its stream
// otherwise than the server does.
namespace
{

QByteArray geometryRequest(const Wire &wire, quint32 drawable)
{
    QByteArray request(8, '\0');
    request[0] = 14;
    wire.word(request, 2, 2);
    wire.integer(request, 4, drawable);
    return request;
}

QByteArray geometryReply(const Wire &wire, quint16 sequence)
{
    QByteArray reply(32, '\0');
    reply[0] = 1;
    wire.word(reply, 2, sequence);
    wire.word(reply, 16, 3840);
    wire.word(reply, 18, 2160);
    return reply;
}

// Requests of the wrong length are the server's to answer with BadLength. The
// relay relays them, reads nothing past their end, and keeps its count of
// requests, so the next reply is still recognised for what it answers.
void malformedRequests(bool little)
{
    Wire wire;
    wire.little = little;
    Policy policy(QSize(2560, 1440));
    setup(policy, wire);
    QByteArray shortGeometry(4, '\0');
    shortGeometry[0] = 14;
    wire.word(shortGeometry, 2, 1);
    check(fragmented(policy, 0, shortGeometry) == shortGeometry, "one-unit GetGeometry changed");
    QByteArray longName(8, '\0');
    longName[0] = 98;
    wire.word(longName, 2, 2);
    wire.word(longName, 4, 200);
    check(fragmented(policy, 0, longName) == longName, "QueryExtension naming past its end changed");
    QByteArray zero(4, '\0');
    zero[0] = 60;
    const QByteArray geometry = geometryRequest(wire, 42);
    check(fragmented(policy, 0, zero + geometry) == zero + geometry, "zero length read as BIG-REQUESTS before they were enabled");
    for (quint16 sequence = 1; sequence <= 3; ++sequence) {
        QByteArray error(32, '\0');
        error[1] = 16;
        wire.word(error, 2, sequence);
        check(fragmented(policy, 1, error) == error, "BadLength changed");
    }
    const QByteArray result = fragmented(policy, 1, geometryReply(wire, 4));
    check(wire.word(result, 16) == 2560 && wire.word(result, 18) == 1440, "the reply after malformed requests was not recognised");
    check(!policy.transparent(), "a malformed request stopped the relay reading its connection");
}

// A RandR screen configuration of the wrong size is relayed, not rewritten
// past its end.
void malformedScreenConfig(bool little)
{
    Wire wire;
    wire.little = little;
    Policy policy(QSize(2560, 1440));
    setup(policy, wire);
    learnExtension(policy, wire, "RANDR", 131, 1);
    QByteArray config(12, '\0');
    config[0] = char(131);
    config[1] = 2;
    wire.word(config, 2, 3);
    check(fragmented(policy, 0, config) == config, "short RRSetScreenConfig changed");
    check(!policy.transparent(), "a short RRSetScreenConfig stopped the relay reading");
}

// BIG-REQUESTS change how a zero length is read only where the server's
// change: after a BigReqEnable of one unit, and not after one of another size.
void bigRequestFraming(bool little)
{
    Wire wire;
    wire.little = little;
    Policy policy;
    setup(policy, wire, false);
    learnExtension(policy, wire, "BIG-REQUESTS", 133, 1);
    QByteArray wrong(8, '\0');
    wrong[0] = char(133);
    wire.word(wrong, 2, 2);
    check(fragmented(policy, 0, wrong) == wrong, "two-unit BigReqEnable changed");
    QByteArray zero(4, '\0');
    zero[0] = 60;
    check(fragmented(policy, 0, zero) == zero, "a refused BigReqEnable enabled BIG-REQUESTS");
    QByteArray enable(4, '\0');
    enable[0] = char(133);
    wire.word(enable, 2, 1);
    check(fragmented(policy, 0, enable) == enable, "BigReqEnable changed");
    QByteArray big(12, '\0');
    big[0] = 60;
    wire.integer(big, 4, 3);
    check(fragmented(policy, 0, big) == big, "a BIG-REQUESTS request misframed");
    // An extended length of one is the four bytes Xwayland consumes; its
    // length field is then read as the start of the next request.
    QByteArray one(8, '\0');
    one[0] = 60;
    wire.integer(one, 4, 1);
    check(policy.feed(0, one) == (little ? one.first(4) : one), "an extended length of one misframed");
    check(!policy.transparent(), "well-formed BIG-REQUESTS stopped the relay reading");
}

// One unit, minor zero, on an opcode the connection never asked about: shaped
// like a BigReqEnable the relay cannot recognise. Were it one, the server
// would read the next zero length otherwise than the relay, so the relay
// stops reading and rewrites nothing more, the root geometry included.
void ambiguousOpcode(bool little)
{
    Wire wire;
    wire.little = little;
    Policy policy(QSize(2560, 1440));
    setup(policy, wire);
    QByteArray guessed(4, '\0');
    guessed[0] = char(200);
    wire.word(guessed, 2, 1);
    check(fragmented(policy, 0, guessed) == guessed, "a request on an unknown opcode changed");
    check(policy.transparent(), "an ambiguous connection was still read");
    const QByteArray geometry = geometryRequest(wire, 42);
    check(fragmented(policy, 0, geometry) == geometry, "a request after the ambiguity changed");
    const QByteArray reply = geometryReply(wire, 2);
    check(fragmented(policy, 1, reply) == reply, "a reply was rewritten on a connection no longer read");
}

// What a client sends never ends its connection from here: a setup in no
// byte order, and a request too large to be one the relay reads, are relayed
// for the server to answer.
void hostileClient(bool little)
{
    Wire wire;
    wire.little = little;
    Policy order;
    const QByteArray nonsense("X\0\0\0\0\0\0\0\0\0\0\0", 12);
    check(order.feed(0, nonsense) == nonsense && order.transparent(), "a setup in no byte order was not relayed");
    Policy policy(QSize(2560, 1440));
    setup(policy, wire);
    enableBigRequests(policy, wire, 1);
    QByteArray huge(8, '\0');
    huge[0] = 98;
    wire.integer(huge, 4, 4 * 1024 * 1024);
    check(policy.feed(0, huge) == huge && policy.transparent(), "an oversized QueryExtension was not relayed");
}

// A socket read may finish a frame at the buffer limit and also contain the
// next frame. The limit bounds retained frames, not the combined read.
void coalescedBoundary(bool little, std::size_t side, bool selected)
{
    Wire wire;
    wire.little = little;
    Policy policy(selected ? QSize(2560, 1440) : QSize());
    setup(policy, wire, selected);
    const quint16 sequence = enableBigRequests(policy, wire, 1);
    for (const qsizetype length : {Policy::s_bufferLimit - 4, Policy::s_bufferLimit, Policy::s_bufferLimit + 4}) {
        QByteArray message(length, '\0');
        message[0] = side == 0 ? 127 : 1;
        wire.integer(message, 4, static_cast<quint32>((length - (side == 0 ? 0 : 32)) / 4));
        QByteArray following(side == 0 ? 8 : 32, '\0');
        following[0] = side == 0 ? 127 : 6;
        if (side == 0) {
            wire.word(following, 2, 2);
        }
        const QByteArray input = message + following;
        QByteArray output = policy.feed(side, input.first(8));
        for (qsizetype offset = 8; offset < input.size(); offset += 65536) {
            output += policy.feed(side, input.mid(offset, 65536));
        }
        check(output == input, "coalesced frame boundary lost bytes");
        check(!policy.pending(side) && !policy.transparent(), "coalesced frame boundary lost framing");
    }
    // Framing still counts requests after the large messages.
    if (side == 0 && selected) {
        check(policy.feed(0, geometryRequest(wire, 42)) == geometryRequest(wire, 42), "geometry request changed");
        const QByteArray result = policy.feed(1, geometryReply(wire, sequence + 6));
        check(wire.word(result, 16) == 2560, "boundary lost request sequence");
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    try {
        for (const bool little : {true, false}) {
            malformedRequests(little);
            malformedScreenConfig(little);
            bigRequestFraming(little);
            ambiguousOpcode(little);
            hostileClient(little);
            for (const bool selected : {true, false}) {
                coalescedBoundary(little, 0, selected);
                coalescedBoundary(little, 1, selected);
            }
        }
    } catch (const std::exception &error) {
        qCritical() << error.what();
        return 1;
    }
    qInfo() << "PASS malformed requests, server framing of BIG-REQUESTS, ambiguous opcodes, hostile clients";
    return 0;
}
