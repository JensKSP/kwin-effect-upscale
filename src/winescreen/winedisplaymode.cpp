/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "winedisplaymode.h"

#include <QList>
#include <QtEndian>

#include <array>

namespace
{

// A DEVMODEW as the Modes value holds it, and the members this code fills, all
// counted from the start of the record. A mode in Current or Registry is the
// same record from dmFields on.
constexpr qsizetype recordSize = 220;
constexpr qsizetype fieldsAt = 72;
constexpr qsizetype positionAt = 76;
constexpr qsizetype specVersionAt = 64;
constexpr qsizetype sizeAt = 68;
constexpr qsizetype depthAt = 168;
constexpr qsizetype widthAt = 172;
constexpr qsizetype heightAt = 176;
constexpr qsizetype rateAt = 184;

// DM_DISPLAYORIENTATION, DM_BITSPERPEL, DM_PELSWIDTH, DM_PELSHEIGHT,
// DM_DISPLAYFLAGS and DM_DISPLAYFREQUENCY: the members a generated mode says
// something about. The current mode adds DM_POSITION for its place on the
// desktop, which is the top left corner here.
constexpr quint32 modeFields = 0x007c0080;
constexpr quint32 positionField = 0x00000020;
constexpr quint32 specVersion = 0x0401;

// The colour depths and the lower rate Wine offers for a screen, whatever the
// screen itself does (dlls/win32u/sysparams.c, get_virtual_modes).
constexpr std::array<quint32, 3> depths = {8, 16, 32};
constexpr int slowRate = 60;

void setNumber(QByteArray &record, qsizetype at, quint32 number)
{
    qToLittleEndian(number, record.data() + at);
}

void setShort(QByteArray &record, qsizetype at, quint16 number)
{
    qToLittleEndian(number, record.data() + at);
}

QByteArray record(const QSize &size, quint32 depth, int refreshRate)
{
    QByteArray mode(recordSize, '\0');
    setShort(mode, specVersionAt, specVersion);
    setShort(mode, sizeAt, static_cast<quint16>(recordSize));
    setNumber(mode, fieldsAt, modeFields);
    setNumber(mode, depthAt, depth);
    setNumber(mode, widthAt, static_cast<quint32>(size.width()));
    setNumber(mode, heightAt, static_cast<quint32>(size.height()));
    setNumber(mode, rateAt, static_cast<quint32>(refreshRate));
    return mode;
}

} // namespace

QByteArray wineDisplayModes(const QSize &size, int refreshRate)
{
    if (size.isEmpty()) {
        return {};
    }
    QList<int> rates = {slowRate};
    if (refreshRate > slowRate) {
        rates.append(refreshRate);
    }
    QByteArray modes;
    modes.reserve(recordSize * static_cast<qsizetype>(depths.size()) * rates.size());
    for (const quint32 depth : depths) {
        for (const int rate : rates) {
            modes.append(record(size, depth, rate));
        }
    }
    return modes;
}

int wineDisplayModeCount(const QByteArray &modes)
{
    return static_cast<int>(modes.size() / recordSize);
}

QByteArray wineDisplayMode(const QRect &rect, int refreshRate)
{
    if (rect.size().isEmpty()) {
        return {};
    }
    QByteArray mode = record(rect.size(), depths.back(), refreshRate);
    setNumber(mode, fieldsAt, modeFields | positionField);
    setNumber(mode, positionAt, static_cast<quint32>(rect.x()));
    setNumber(mode, positionAt + 4, static_cast<quint32>(rect.y()));
    return mode.last(recordSize - fieldsAt);
}

std::optional<int> wineDisplayModeRate(const QByteArray &value)
{
    if (value.size() < recordSize - fieldsAt) {
        return std::nullopt;
    }
    const quint32 rate = qFromLittleEndian<quint32>(value.constData() + rateAt - fieldsAt);
    return rate == 0 ? std::nullopt : std::optional(static_cast<int>(rate));
}

std::optional<QRect> wineDisplayModeRect(const QByteArray &value)
{
    if (value.size() < recordSize - fieldsAt) {
        return std::nullopt;
    }
    const auto number = [&value](qsizetype at) {
        return qFromLittleEndian<quint32>(value.constData() + at - fieldsAt);
    };
    const QSize size(static_cast<int>(number(widthAt)), static_cast<int>(number(heightAt)));
    const QPoint position(static_cast<int>(number(positionAt)), static_cast<int>(number(positionAt + 4)));
    return size.isEmpty() ? std::nullopt : std::optional(QRect(position, size));
}
