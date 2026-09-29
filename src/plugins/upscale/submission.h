/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QString>
#include <QVariantMap>

namespace KWin
{

/**
 * The report a person sends with an application entry: the entry as the
 * effect observed the window, in kwinupscalerc's own format, and the
 * conditions it was observed under as comments, with what only the person
 * knows left as marked blanks.
 *
 * @p facts is what UpscaleIdentityService::reportFacts() answered, and
 * @p system the distribution it ran on. The program is stated as it stays the
 * same wherever the game is installed, never by its path; a window's title,
 * the environment and anything else about the person are not in @p facts and
 * never in the report.
 *
 * Written in English and not translated: it is read on the project's tracker,
 * and its lines are the entry's own keys.
 */
QString upscaleSubmissionReport(const QVariantMap &facts, const QString &system);

} // namespace KWin
