/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "pattern.h"

#include <KLocalizedString>

#include <utility>

namespace KWin
{

QString upscaleStringMatchKey(UpscaleStringMatch match)
{
    switch (match) {
    case UpscaleStringMatch::Substring:
        return QStringLiteral("Substring");
    case UpscaleStringMatch::RegularExpression:
        return QStringLiteral("RegularExpression");
    case UpscaleStringMatch::Exact:
        break;
    }
    return QStringLiteral("Exact");
}

UpscaleStringMatch upscaleStringMatchFromKey(const QString &name)
{
    if (name == QLatin1String("Substring")) {
        return UpscaleStringMatch::Substring;
    }
    if (name == QLatin1String("RegularExpression")) {
        return UpscaleStringMatch::RegularExpression;
    }
    return UpscaleStringMatch::Exact;
}

UpscalePattern::UpscalePattern(QString text, UpscaleStringMatch match)
    : m_text(std::move(text))
    , m_match(match)
{
    if (m_match == UpscaleStringMatch::RegularExpression && !m_text.isEmpty()) {
        m_expression.setPattern(QRegularExpression::anchoredPattern(m_text));
        m_usable = m_expression.isValid() && !m_expression.match(QString()).hasMatch();
    }
}

bool UpscalePattern::isStated() const
{
    return !m_text.isEmpty();
}

bool UpscalePattern::matches(const QString &value) const
{
    switch (m_match) {
    case UpscaleStringMatch::Exact:
        // Case included, as window identity has always been compared: a
        // window class is an identifier, not a name.
        return value == m_text;
    case UpscaleStringMatch::Substring:
        return !value.isEmpty() && value.contains(m_text);
    case UpscaleStringMatch::RegularExpression:
        return m_usable && !value.isEmpty() && m_expression.match(value).hasMatch();
    }
    return false;
}

QString UpscalePattern::problem() const
{
    if (m_match != UpscaleStringMatch::RegularExpression || m_text.isEmpty()) {
        return QString();
    }
    if (!m_expression.isValid()) {
        return i18nc("%1 is the pattern, %2 what is wrong with it", "“%1” is not a valid regular expression: %2", m_text,
                     m_expression.errorString());
    }
    if (m_expression.match(QString()).hasMatch()) {
        return i18nc("%1 is the pattern", "“%1” matches everything.", m_text);
    }
    return QString();
}

} // namespace KWin
