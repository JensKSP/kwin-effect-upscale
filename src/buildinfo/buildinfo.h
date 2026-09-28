/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

/**
 * Which build this is. The values are filled in by buildinfo.cpp.in, which the
 * build regenerates from git whenever the commit changes.
 *
 * This header holds declarations only and never changes, so a new commit
 * recompiles one translation unit rather than everything that asks what version
 * it is. That is the whole reason it is a header and a generated source rather
 * than a generated header.
 *
 * It lives outside src/plugins/upscale/ because KWin has nothing like it: the
 * plugin folder must stay a folder KDE could copy into KWin unchanged.
 */

#pragma once

#include <QString>
#include <QStringList>

namespace KWin
{
namespace UpscaleBuildInfo
{

/**
 * The version this build calls itself: "0.1.0" on a release tag, and
 * "0.1.0+git20260917.abc1234def" or the same with "-dirty" appended anywhere
 * else. Ordered the way Debian orders a snapshot taken after a release.
 */
QString version();

/** The three-part version declared by the top-level CMake project. */
QString baseVersion();

/**
 * The abbreviated commit, also on release tags, with a dirty suffix for local
 * changes. Empty when the source carries no revision information.
 */
QString revision();

/**
 * The full commit the build came from, also on release tags, where the
 * version carries no hash. Empty when neither git nor the source archive
 * recorded it.
 */
QString commit();

/**
 * The branch the build came from, empty when it was not built from one: a
 * detached checkout, a build for a tag, or a source archive that did not
 * record it.
 */
QString branch();

/** The tag on the commit the build came from, empty when it carries none. */
QString tag();

/**
 * When the build ran, as ISO 8601 in UTC. A packaged build reports the date of
 * its changelog entry rather than the wall clock, so that the package stays
 * reproducible.
 */
QString buildDate();

/**
 * Whether buildDate() is the one SOURCE_DATE_EPOCH gave rather than the time
 * the build ran.
 */
bool reproducibleBuildDate();

/**
 * One line naming the version, the branch and the tag, the abbreviated commit,
 * the build date and the Qt the plugin was built against. This is the build
 * line of the developer information.
 */
QString describe();

/**
 * The whole record, one labelled field per line: describe()'s fields, the Qt
 * the process runs with, the project, the license and where the third-party
 * notices are installed. What the log gets when the effect starts.
 */
QStringList record();

/**
 * Writes record() to the log at information level, once per call.
 *
 * The effect calls this when it initializes. It is deliberately not a static
 * initializer: this unit is also linked into the settings module, which would
 * otherwise announce an effect that process never loaded.
 */
void announce();

} // namespace UpscaleBuildInfo
} // namespace KWin
