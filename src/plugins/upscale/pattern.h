/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QRegularExpression>
#include <QString>

namespace KWin
{

/**
 * How one identity field of a profile is compared.
 *
 * The three KWin's own Window Rules offer for a string property
 * (Rules::StringMatch), without its "unimportant": a field left empty already
 * says that here.
 */
enum class UpscaleStringMatch {
    Exact,
    Substring,
    RegularExpression,
};

/** The configuration name of a match type, as the stored file spells it. */
QString upscaleStringMatchKey(UpscaleStringMatch match);

/**
 * A match type from the name a file spells.
 *
 * An absent or unknown name reads as Exact, the narrowest of the three: a
 * later version's match type misread as a wider one could claim windows the
 * entry was never meant to reach.
 */
UpscaleStringMatch upscaleStringMatchFromKey(const QString &name);

/**
 * One stated identity field, compiled once for comparison.
 *
 * A regular expression is anchored to the whole value, so that hl2_linux
 * does not also match …/hl2_linux_old; a pattern that wants part of a value
 * says so. It is compiled here, when configuration is read, and never while
 * a frame is being painted.
 */
class UpscalePattern
{
public:
    UpscalePattern() = default;
    UpscalePattern(QString text, UpscaleStringMatch match);

    /** Whether the field constrains anything. An empty one does not. */
    bool isStated() const;

    /** Whether @p value satisfies this field. Never true for a pattern with a problem. */
    bool matches(const QString &value) const;

    /**
     * Why this pattern cannot be used, or an empty string.
     *
     * Two reasons. An invalid regular expression would otherwise match
     * nothing without saying so. And a pattern that matches the empty string,
     * such as .*, matches every value: it is the entry that constrains
     * nothing, which is refused because it would claim the desktop's own
     * windows, in another form.
     */
    QString problem() const;

private:
    QString m_text;
    UpscaleStringMatch m_match = UpscaleStringMatch::Exact;
    QRegularExpression m_expression;
    // Decided when the pattern is built, so that comparing never repeats it.
    bool m_usable = true;
};

/**
 * What is known about a window, or about a connection before it has one.
 *
 * The executable is empty where it could not be resolved, which is a fact
 * about this window at this launch rather than about the program: see
 * upscaleApplicationFor().
 */
struct UpscaleIdentity
{
    QString executable;
    QString windowClass;
    QString instance;
};

} // namespace KWin
