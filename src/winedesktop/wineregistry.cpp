/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineregistry.h"

#include "winedisplaymode.h"

#include <QList>

namespace
{

// The format is Wine's own, written by server/registry.c: a header line, then
// one section per key, each opened by a line "[name] modification-time" and
// followed by its values, one "name"=data line each unless binary data
// continues on the next line after a trailing backslash. Backslashes in names
// are doubled. Wine compares key and value names without regard to case.
const QByteArray registryHeader = QByteArrayLiteral("WINE REGISTRY Version 2");
const QByteArray explorerKey = QByteArrayLiteral("Software\\\\Wine\\\\Explorer");
const QByteArray desktopsKey = QByteArrayLiteral("Software\\\\Wine\\\\Explorer\\\\Desktops");
const QByteArray desktopValue = QByteArrayLiteral("Desktop");
const QByteArray defaultValue = QByteArrayLiteral("Default");

// Where win32u looks for the screen: the value that names the source key, and
// the source key itself, below the current hardware profile.
const QByteArray videoMapKey = QByteArrayLiteral("HARDWARE\\\\DEVICEMAP\\\\VIDEO");
const QByteArray videoValue = QByteArrayLiteral("\\\\Device\\\\Video0");
const QByteArray sourceKeys = QByteArrayLiteral(
    "System\\\\ControlSet001\\\\Hardware Profiles\\\\Current\\\\System\\\\CurrentControlSet\\\\Control\\\\Video\\\\");
const QByteArray sourceKeyEnd = QByteArrayLiteral("\\\\0000");
const QByteArray sourcePath = QByteArrayLiteral("\\\\Registry\\\\Machine\\\\System\\\\CurrentControlSet\\\\Control\\\\Video\\\\");

// What the source key says about the screen.
const QByteArray currentValue = QByteArrayLiteral("Current");
const QByteArray registryValue = QByteArrayLiteral("Registry");
const QByteArray dpiValue = QByteArrayLiteral("Dpi");
const QByteArray cardValue = QByteArrayLiteral("GPUID");
const QByteArray monitorValue = QByteArrayLiteral("MonitorID0");
const QByteArray modesValue = QByteArrayLiteral("Modes");
const QByteArray modeCountValue = QByteArrayLiteral("ModeCount");
const QByteArray stateFlagsValue = QByteArrayLiteral("StateFlags");

// The dots per inch a screen has unless something says otherwise, and the two
// state flags a screen in use has: DISPLAY_DEVICE_ATTACHED_TO_DESKTOP and
// DISPLAY_DEVICE_PRIMARY_DEVICE. A screen without the first has no size at all.
constexpr quint32 standardDpi = 96;
constexpr quint32 attachedAndPrimary = 5;
constexpr int standardRate = 60;

// Where the prefix describes the devices it found when it last asked, in keys
// that outlast a session.
const QByteArray enumKeys = QByteArrayLiteral("System\\\\ControlSet001\\\\Enum\\\\");
const QByteArray cardKind = QByteArrayLiteral("PCI");
const QByteArray monitorKind = QByteArrayLiteral("DISPLAY");
const QByteArray parametersKey = QByteArrayLiteral("Device Parameters");
const QByteArray cardIdValue = QByteArrayLiteral("VideoID");
const QByteArray separator = QByteArrayLiteral("\\\\");

// Lines [key, end) of one key's section.
struct Section
{
    qsizetype key = -1;
    qsizetype end = -1;
};

// Lines [first, end) of one value, continuation lines included.
struct ValueLines
{
    qsizetype first = -1;
    qsizetype end = -1;
};

bool isKeyLine(const QByteArray &line)
{
    return line.startsWith('[');
}

bool startsWithIgnoringCase(const QByteArray &line, const QByteArray &prefix)
{
    return line.size() >= prefix.size() && line.first(prefix.size()).compare(prefix, Qt::CaseInsensitive) == 0;
}

bool opensKey(const QByteArray &line, const QByteArray &name)
{
    // The closing bracket right after the name keeps Explorer from matching
    // Explorer\\Desktops.
    return startsWithIgnoringCase(line, '[' + name + ']');
}

// The name a section's first line opens, "[name] modification-time".
QByteArray keyName(const QByteArray &line)
{
    const qsizetype end = line.lastIndexOf(']');
    return end < 1 ? QByteArray() : line.mid(1, end - 1);
}

Section sectionFrom(const QList<QByteArray> &lines, qsizetype key)
{
    qsizetype end = key + 1;
    while (end < lines.size() && !isKeyLine(lines[end])) {
        ++end;
    }
    return {key, end};
}

Section findSection(const QList<QByteArray> &lines, const QByteArray &name)
{
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (opensKey(lines[index], name)) {
            return sectionFrom(lines, index);
        }
    }
    return {};
}

// The first section whose key name begins with that text, which is how a key
// with a name this companion does not know, an identifier of the prefix's own,
// is found again.
Section findSectionStarting(const QList<QByteArray> &lines, const QByteArray &start)
{
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (isKeyLine(lines[index]) && startsWithIgnoringCase(lines[index], '[' + start)) {
            return sectionFrom(lines, index);
        }
    }
    return {};
}

QByteArray valuePrefix(const QByteArray &name)
{
    return '"' + name + "\"=";
}

ValueLines findValue(const QList<QByteArray> &lines, const Section &section, const QByteArray &name)
{
    const QByteArray prefix = valuePrefix(name);
    for (qsizetype index = section.key + 1; index < section.end; ++index) {
        if (!startsWithIgnoringCase(lines[index], prefix)) {
            continue;
        }
        qsizetype end = index + 1;
        while (end < section.end && lines[end - 1].endsWith('\\')) {
            ++end;
        }
        return {index, end};
    }
    return {};
}

// A plain string value, "name"="text", with Wine's backslash escapes undone.
std::optional<QString> stringIn(const QList<QByteArray> &lines, const Section &section, const QByteArray &name)
{
    const ValueLines value = findValue(lines, section, name);
    if (value.first < 0 || value.end != value.first + 1) {
        return std::nullopt;
    }
    const QByteArray data = lines[value.first].mid(valuePrefix(name).size());
    if (data.size() < 2 || !data.startsWith('"') || !data.endsWith('"')) {
        return std::nullopt;
    }
    QByteArray text;
    for (qsizetype index = 1; index + 1 < data.size(); ++index) {
        if (data.at(index) == '\\' && index + 2 < data.size()) {
            ++index;
        }
        text.append(data.at(index));
    }
    return QString::fromLatin1(text);
}

std::optional<QString> stringValue(const QList<QByteArray> &lines, const QByteArray &key, const QByteArray &name)
{
    const Section section = findSection(lines, key);
    return section.key < 0 ? std::nullopt : stringIn(lines, section, name);
}

// Binary data, "name"=hex:aa,bb, its continuation lines joined again.
QByteArray binaryIn(const QList<QByteArray> &lines, const Section &section, const QByteArray &name)
{
    const ValueLines value = findValue(lines, section, name);
    if (value.first < 0) {
        return {};
    }
    QByteArray data;
    for (qsizetype index = value.first; index < value.end; ++index) {
        QByteArray part = lines[index].trimmed();
        if (index == value.first) {
            part = part.mid(valuePrefix(name).size());
            if (!startsWithIgnoringCase(part, QByteArrayLiteral("hex:"))) {
                return {};
            }
            part = part.mid(4);
        }
        if (part.endsWith('\\')) {
            part.chop(1);
        }
        data.append(part);
    }
    return QByteArray::fromHex(data);
}

QByteArray textValueLine(const QByteArray &name, const QByteArray &text)
{
    return valuePrefix(name) + '"' + text + '"';
}

QByteArray numberValueLine(const QByteArray &name, quint32 number)
{
    return valuePrefix(name) + "dword:" + QByteArray::number(number, 16).rightJustified(8, '0');
}

// Binary data as Wine writes it: two digits a byte, separated by commas, broken
// into lines of its own width with a backslash at the end of each.
QList<QByteArray> binaryValueLines(const QByteArray &name, const QByteArray &data)
{
    constexpr qsizetype width = 76;
    QList<QByteArray> lines;
    QByteArray line = valuePrefix(name) + "hex:";
    for (qsizetype index = 0; index < data.size(); ++index) {
        const QByteArray piece = data.mid(index, 1).toHex() + (index + 1 < data.size() ? "," : "");
        if (line.size() + piece.size() > width) {
            lines.append(line + '\\');
            line = QByteArrayLiteral("  ");
        }
        line.append(piece);
    }
    lines.append(line);
    return lines;
}

void appendKey(QList<QByteArray> &lines, const QByteArray &key, const QList<QByteArray> &values, qint64 modifiedSeconds)
{
    // Split on newlines, a file that ends with one ends in an empty element;
    // keep that shape so the file still ends with a newline afterwards.
    if (!lines.isEmpty() && lines.last().isEmpty()) {
        lines.removeLast();
    }
    if (!lines.isEmpty() && !lines.last().isEmpty()) {
        lines.append(QByteArray());
    }
    lines.append('[' + key + "] " + QByteArray::number(modifiedSeconds));
    lines.append(values);
    lines.append(QByteArray());
}

// Every section whose key name begins with that text, with the empty line that
// separates it from the one before.
void removeSections(QList<QByteArray> &lines, const QByteArray &start)
{
    for (Section section = findSectionStarting(lines, start); section.key >= 0; section = findSectionStarting(lines, start)) {
        qsizetype first = section.key;
        if (first > 0 && lines[first - 1].isEmpty()) {
            --first;
        }
        lines.remove(first, section.end - first);
    }
    // A file Wine wrote ends with a newline; removing its last key keeps that.
    if (!lines.isEmpty() && !lines.last().isEmpty()) {
        lines.append(QByteArray());
    }
}

// A key name below Enum, split at the doubled backslashes: the kind of device,
// then what the prefix calls this one.
QList<QByteArray> devicePath(const QByteArray &name)
{
    QList<QByteArray> parts;
    for (qsizetype at = 0; at <= name.size();) {
        const qsizetype next = name.indexOf(separator, at);
        if (next < 0) {
            parts.append(name.mid(at));
            break;
        }
        parts.append(name.mid(at, next - at));
        at = next + separator.size();
    }
    return parts;
}

bool isKind(const QList<QByteArray> &parts, const QByteArray &kind, qsizetype count)
{
    return parts.size() == count && parts.first().compare(kind, Qt::CaseInsensitive) == 0;
}

} // namespace

bool WineScreenDevices::isEmpty() const
{
    return card.isEmpty() || cardId.isEmpty() || monitor.isEmpty();
}

bool wineIsRegistry(const QByteArray &text)
{
    // The whole first line: "Version 20" would be another format.
    const qsizetype end = text.indexOf('\n');
    QByteArrayView line = end < 0 ? QByteArrayView(text) : QByteArrayView(text).first(end);
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    return line == registryHeader;
}

WineDesktopValues wineDesktopValues(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    return {
        .desktop = stringValue(lines, explorerKey, desktopValue),
        .defaultSize = stringValue(lines, desktopsKey, defaultValue),
    };
}

WineScreenDevices wineScreenDevices(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    WineScreenDevices devices;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (!isKeyLine(lines[index])) {
            continue;
        }
        const QByteArray name = keyName(lines[index]);
        if (!startsWithIgnoringCase(name, enumKeys)) {
            continue;
        }
        const QByteArray device = name.mid(enumKeys.size());
        const QList<QByteArray> parts = devicePath(device);
        // The card's own identifier is the name of the key its screen goes under,
        // and it is kept where the card describes itself.
        if (devices.card.isEmpty() && isKind(parts, cardKind, 4) && parts.last() == parametersKey) {
            if (const std::optional<QString> id = stringIn(lines, sectionFrom(lines, index), cardIdValue)) {
                devices.card = device.first(device.size() - parametersKey.size() - separator.size());
                devices.cardId = id->toLatin1();
            }
        }
        if (devices.monitor.isEmpty() && isKind(parts, monitorKind, 3)) {
            devices.monitor = device;
        }
    }
    return devices;
}

std::optional<QSize> wineScreen(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    const Section section = findSectionStarting(lines, sourceKeys);
    if (section.key < 0) {
        return std::nullopt;
    }
    return wineDisplayModeSize(binaryIn(lines, section, currentValue));
}

std::optional<QByteArray> wineWithScreen(const QByteArray &text, const QSize &size, int refreshRate, qint64 modifiedSeconds)
{
    const WineScreenDevices devices = wineScreenDevices(text);
    if (!wineIsRegistry(text) || size.isEmpty() || devices.isEmpty()) {
        return std::nullopt;
    }
    QList<QByteArray> lines = text.split('\n');
    removeSections(lines, videoMapKey);
    removeSections(lines, sourceKeys);

    const QByteArray source = devices.cardId + sourceKeyEnd;
    appendKey(lines, videoMapKey, {textValueLine(videoValue, sourcePath + source)}, modifiedSeconds);

    const QByteArray modes = wineDisplayModes(size, refreshRate);
    QList<QByteArray> values = binaryValueLines(currentValue, wineDisplayMode(size, 0));
    values.append(numberValueLine(dpiValue, standardDpi));
    values.append(textValueLine(cardValue, devices.card));
    values.append(numberValueLine(modeCountValue, static_cast<quint32>(wineDisplayModeCount(modes))));
    values.append(binaryValueLines(modesValue, modes));
    values.append(textValueLine(monitorValue, devices.monitor));
    values.append(binaryValueLines(registryValue, wineDisplayMode(size, refreshRate > 0 ? refreshRate : standardRate)));
    values.append(numberValueLine(stateFlagsValue, attachedAndPrimary));
    appendKey(lines, sourceKeys + source, values, modifiedSeconds);
    return lines.join('\n');
}

std::optional<QByteArray> wineWithoutScreen(const QByteArray &text)
{
    if (!wineIsRegistry(text)) {
        return std::nullopt;
    }
    QList<QByteArray> lines = text.split('\n');
    removeSections(lines, videoMapKey);
    removeSections(lines, sourceKeys);
    return lines.join('\n');
}
