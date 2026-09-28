/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineregistry.h"

#include "winedisplaymode.h"

#include <QList>
#include <QStringList>

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
const QByteArray videoValue = QByteArrayLiteral("\\\\Device\\\\Video");
const QByteArray sourceKeys = QByteArrayLiteral(
    "System\\\\ControlSet001\\\\Hardware Profiles\\\\Current\\\\System\\\\CurrentControlSet\\\\Control\\\\Video\\\\");
const QByteArray monitorValuePrefix = QByteArrayLiteral("MonitorID");
const QByteArray sourcePath = QByteArrayLiteral("\\\\Registry\\\\Machine\\\\System\\\\CurrentControlSet\\\\Control\\\\Video\\\\");

// What the source key says about the screen.
const QByteArray currentValue = QByteArrayLiteral("Current");
const QByteArray registryValue = QByteArrayLiteral("Registry");
const QByteArray dpiValue = QByteArrayLiteral("Dpi");
const QByteArray cardValue = QByteArrayLiteral("GPUID");
const QByteArray modesValue = QByteArrayLiteral("Modes");
const QByteArray modeCountValue = QByteArrayLiteral("ModeCount");
const QByteArray stateFlagsValue = QByteArrayLiteral("StateFlags");

// The dots per inch a screen has unless something says otherwise, and the two
// state flags a screen in use has: DISPLAY_DEVICE_ATTACHED_TO_DESKTOP and
// DISPLAY_DEVICE_PRIMARY_DEVICE. A screen without the first has no size at all.
constexpr quint32 standardDpi = 96;
constexpr quint32 attached = 1;
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

// What follows a card's own name in a screen's key: the screen's number among
// that card's, as Wine writes it.
QByteArray sourceIndex(qsizetype index)
{
    return separator + QByteArray::number(index, 16).rightJustified(4, '0');
}

// The rate a described screen runs at, which the registry mode carries; the
// current mode leaves it unsaid, as Wine's own does.
int rateIn(const QList<QByteArray> &lines, const Section &section)
{
    return wineDisplayModeRate(binaryIn(lines, section, registryValue)).value_or(0);
}

} // namespace

bool WineScreenDevices::isEmpty() const
{
    return card.isEmpty() || cardId.isEmpty() || monitors.isEmpty();
}

QString wineScreensText(const QList<WineScreen> &screens)
{
    QStringList described;
    described.reserve(screens.size());
    for (const WineScreen &screen : screens) {
        described.append(QStringLiteral("%1x%2+%3+%4@%5")
                             .arg(screen.rect.width())
                             .arg(screen.rect.height())
                             .arg(screen.rect.x())
                             .arg(screen.rect.y())
                             .arg(screen.rate));
    }
    return described.join(QLatin1Char(';'));
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
        if (isKind(parts, monitorKind, 3)) {
            devices.monitors.append(device);
        }
    }
    return devices;
}

QList<WineScreen> wineScreens(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    QList<WineScreen> screens;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (!isKeyLine(lines[index]) || !startsWithIgnoringCase(lines[index], '[' + sourceKeys)) {
            continue;
        }
        const Section section = sectionFrom(lines, index);
        const std::optional<QRect> rect = wineDisplayModeRect(binaryIn(lines, section, currentValue));
        if (!rect) {
            continue;
        }
        screens.append({.rect = *rect, .rate = rateIn(lines, section)});
    }
    return screens;
}

std::optional<QByteArray> wineWithScreens(const QByteArray &text, const QList<WineScreen> &screens, qint64 modifiedSeconds)
{
    const WineScreenDevices devices = wineScreenDevices(text);
    if (!wineIsRegistry(text) || screens.isEmpty() || devices.isEmpty()) {
        return std::nullopt;
    }
    // A screen needs a monitor of its own, or Wine gives it no size at all.
    const qsizetype count = std::min(screens.size(), devices.monitors.size());
    for (qsizetype index = 0; index < count; ++index) {
        if (screens.at(index).rect.size().isEmpty()) {
            return std::nullopt;
        }
    }
    QList<QByteArray> lines = text.split('\n');
    removeSections(lines, videoMapKey);
    removeSections(lines, sourceKeys);

    QList<QByteArray> map;
    for (qsizetype index = 0; index < count; ++index) {
        const QByteArray source = devices.cardId + sourceIndex(index);
        map.append(textValueLine(videoValue + QByteArray::number(index), sourcePath + source));
    }
    appendKey(lines, videoMapKey, map, modifiedSeconds);

    for (qsizetype index = 0; index < count; ++index) {
        const WineScreen &screen = screens.at(index);
        const QByteArray modes = wineDisplayModes(screen.rect.size(), screen.rate);
        QList<QByteArray> values = binaryValueLines(currentValue, wineDisplayMode(screen.rect, 0));
        values.append(numberValueLine(dpiValue, standardDpi));
        values.append(textValueLine(cardValue, devices.card));
        values.append(numberValueLine(modeCountValue, static_cast<quint32>(wineDisplayModeCount(modes))));
        values.append(binaryValueLines(modesValue, modes));
        values.append(textValueLine(monitorValuePrefix + "0", devices.monitors.at(index)));
        values.append(binaryValueLines(registryValue, wineDisplayMode(screen.rect, screen.rate > 0 ? screen.rate : standardRate)));
        // The screen the prepared program is on comes first and is the primary
        // one, which is where a program puts what it does not place itself.
        values.append(numberValueLine(stateFlagsValue, index == 0 ? attachedAndPrimary : attached));
        appendKey(lines, sourceKeys + devices.cardId + sourceIndex(index), values, modifiedSeconds);
    }
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
