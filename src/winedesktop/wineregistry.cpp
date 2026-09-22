/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "wineregistry.h"

#include <QList>

namespace
{

// The format is Wine's own, written by server/registry.c: a header line, then
// one section per key, each opened by a line "[name] modification-time" and
// followed by its values, one "name"=data line each unless binary data
// continues on the next line after a trailing backslash. Backslashes in names
// are doubled. Wine compares key and value names without regard to case.
const QByteArray s_header = QByteArrayLiteral("WINE REGISTRY Version 2");
const QByteArray s_explorerKey = QByteArrayLiteral("Software\\\\Wine\\\\Explorer");
const QByteArray s_desktopsKey = QByteArrayLiteral("Software\\\\Wine\\\\Explorer\\\\Desktops");
const QByteArray s_desktopValue = QByteArrayLiteral("Desktop");
const QByteArray s_defaultValue = QByteArrayLiteral("Default");

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

Section findSection(const QList<QByteArray> &lines, const QByteArray &name)
{
    for (qsizetype index = 0; index < lines.size(); ++index) {
        if (!opensKey(lines[index], name)) {
            continue;
        }
        qsizetype end = index + 1;
        while (end < lines.size() && !isKeyLine(lines[end])) {
            ++end;
        }
        return {index, end};
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
std::optional<QString> stringValue(const QList<QByteArray> &lines, const QByteArray &key, const QByteArray &name)
{
    const Section section = findSection(lines, key);
    if (section.key < 0) {
        return std::nullopt;
    }
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

void appendKey(QList<QByteArray> &lines, const QByteArray &key, const QByteArray &valueLine, qint64 modifiedSeconds)
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
    lines.append(valueLine);
    lines.append(QByteArray());
}

void setValue(QList<QByteArray> &lines, const QByteArray &key, const QByteArray &name, const QByteArray &text, qint64 modifiedSeconds)
{
    const QByteArray valueLine = valuePrefix(name) + '"' + text + '"';
    const Section section = findSection(lines, key);
    if (section.key < 0) {
        appendKey(lines, key, valueLine, modifiedSeconds);
        return;
    }
    const ValueLines existing = findValue(lines, section, name);
    if (existing.first >= 0) {
        lines.remove(existing.first, existing.end - existing.first);
        lines.insert(existing.first, valueLine);
        return;
    }
    // New values go after the key's own metadata lines (#time=, #class=).
    qsizetype at = section.key + 1;
    while (at < section.end && lines[at].startsWith('#')) {
        ++at;
    }
    lines.insert(at, valueLine);
}

void removeValue(QList<QByteArray> &lines, const QByteArray &key, const QByteArray &name)
{
    const Section section = findSection(lines, key);
    if (section.key < 0) {
        return;
    }
    const ValueLines existing = findValue(lines, section, name);
    if (existing.first >= 0) {
        lines.remove(existing.first, existing.end - existing.first);
    }
}

} // namespace

bool wineIsRegistry(const QByteArray &text)
{
    // The whole first line: "Version 20" would be another format.
    const qsizetype end = text.indexOf('\n');
    QByteArrayView line = end < 0 ? QByteArrayView(text) : QByteArrayView(text).first(end);
    if (line.endsWith('\r')) {
        line.chop(1);
    }
    return line == s_header;
}

WineDesktopValues wineDesktopValues(const QByteArray &text)
{
    const QList<QByteArray> lines = text.split('\n');
    return {
        .desktop = stringValue(lines, s_explorerKey, s_desktopValue),
        .defaultSize = stringValue(lines, s_desktopsKey, s_defaultValue),
    };
}

std::optional<QByteArray> wineWithVirtualDesktop(const QByteArray &text, const QSize &size, qint64 modifiedSeconds)
{
    if (!wineIsRegistry(text) || size.isEmpty()) {
        return std::nullopt;
    }
    QList<QByteArray> lines = text.split('\n');
    setValue(lines, s_explorerKey, s_desktopValue, s_defaultValue, modifiedSeconds);
    const QByteArray sizeText = QByteArray::number(size.width()) + 'x' + QByteArray::number(size.height());
    setValue(lines, s_desktopsKey, s_defaultValue, sizeText, modifiedSeconds);
    return lines.join('\n');
}

std::optional<QByteArray> wineWithoutVirtualDesktop(const QByteArray &text)
{
    if (!wineIsRegistry(text)) {
        return std::nullopt;
    }
    QList<QByteArray> lines = text.split('\n');
    removeValue(lines, s_explorerKey, s_desktopValue);
    removeValue(lines, s_desktopsKey, s_defaultValue);
    return lines.join('\n');
}
