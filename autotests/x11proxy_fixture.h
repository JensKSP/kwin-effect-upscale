// SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "policy.h"
#include <stdexcept>

// What the relay's tests say to it: a connection set up, an extension learnt,
// BIG-REQUESTS enabled, each the way a client and the server say it.
namespace UpscaleX11Test
{

inline void check(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Fed one byte at a time, because the relay must read a record the same way
// however the socket happens to cut it.
inline QByteArray fragmented(UpscaleX11::Policy &policy, std::size_t side, const QByteArray &bytes)
{
    QByteArray result;
    for (const char byte : bytes) {
        result += policy.feed(side, QByteArray(1, byte));
    }
    check(!policy.pending(side), "incomplete fixture");
    return result;
}

inline void setup(UpscaleX11::Policy &policy, const UpscaleX11::Wire &wire, bool selected = true)
{
    QByteArray request(12, '\0');
    request[0] = wire.little ? 'l' : 'B';
    wire.word(request, 2, 11);
    check(fragmented(policy, 0, request) == request, "setup request changed");
    QByteArray response(80, '\0');
    response[0] = 1;
    wire.word(response, 2, 11);
    wire.word(response, 6, 18);
    response[28] = 1;
    wire.integer(response, 40, 42);
    wire.word(response, 60, 3840);
    wire.word(response, 62, 2160);
    QByteArray expected = response;
    if (selected) {
        wire.word(expected, 60, 2560);
        wire.word(expected, 62, 1440);
    }
    check(fragmented(policy, 1, response) == expected, "setup root dimensions");
}

// Asks the server about an extension and answers that it is there under
// @p major. Returns the sequence number of the next request.
inline quint16 learnExtension(UpscaleX11::Policy &policy, const UpscaleX11::Wire &wire, const QByteArray &name, quint8 major, quint16 sequence)
{
    const qsizetype padded = (name.size() + 3) & ~qsizetype(3);
    QByteArray query(8 + padded, '\0');
    query[0] = 98;
    wire.word(query, 2, static_cast<quint16>(query.size() / 4));
    wire.word(query, 4, static_cast<quint16>(name.size()));
    query.replace(8, name.size(), name);
    check(fragmented(policy, 0, query) == query, "extension query changed");
    QByteArray reply(32, '\0');
    reply[0] = 1;
    wire.word(reply, 2, sequence);
    reply[8] = 1;
    reply[9] = static_cast<char>(major);
    check(fragmented(policy, 1, reply) == reply, "extension reply changed");
    return sequence + 1;
}

// BIG-REQUESTS, learnt under 133 and enabled. Returns the next sequence number.
inline quint16 enableBigRequests(UpscaleX11::Policy &policy, const UpscaleX11::Wire &wire, quint16 sequence)
{
    sequence = learnExtension(policy, wire, "BIG-REQUESTS", 133, sequence);
    QByteArray enable(4, '\0');
    enable[0] = char(133);
    wire.word(enable, 2, 1);
    check(fragmented(policy, 0, enable) == enable, "BigReqEnable changed");
    QByteArray reply(32, '\0');
    reply[0] = 1;
    wire.word(reply, 2, sequence);
    wire.integer(reply, 8, 4194303);
    check(fragmented(policy, 1, reply) == reply, "BigReqEnable reply changed");
    return sequence + 1;
}

} // namespace UpscaleX11Test
