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

/**
 * The branch the build came from, empty when git could not say. A release
 * tarball has no git and therefore no branch.
 */
QString branch();

/**
 * When the build ran, as ISO 8601 in UTC. A packaged build reports the date of
 * its changelog entry rather than the wall clock, so that the package stays
 * reproducible.
 */
QString buildDate();

/**
 * One line naming the version, the branch, the build date and the Qt the
 * plugin was built against. This is what goes in a log and in an about box.
 */
QString describe();

} // namespace UpscaleBuildInfo
} // namespace KWin
