/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "application.h"
#include "pattern.h"

namespace KWin
{

/**
 * A profile's identity, compiled: gate 1 and the two fields of gate 2.
 *
 * Gate 1 is the program's executable path, gate 2 the window's class and
 * instance. The rule is that a profile states at least one gate and matches
 * when every gate it states matches. Stating both is the precise case; gate 1
 * alone is what a native Wayland game needs, because it is the only identity
 * there is before the window exists; gate 2 alone is for the two cases where
 * a path cannot say which game this is - a loader or interpreter shared by
 * many games, and a window whose path did not resolve.
 */
struct UpscaleGates
{
    UpscalePattern executable;
    UpscalePattern windowClass;
    UpscalePattern instance;

    bool statesExecutable() const;
    bool statesWindow() const;
    /** Every gate stated matches, and at least one is stated. */
    bool matches(const UpscaleIdentity &identity) const;
};

/** The identity fields of @p application, compiled for matching. */
UpscaleGates upscaleGatesOf(const UpscaleApplication &application);

/**
 * Why @p application can never match a window, or an empty string.
 *
 * It states no gate at all, which would claim every window and is dropped
 * when the list is read, or one of its patterns cannot be used. The editor
 * refuses to store either; an entry written by hand with a bad pattern is
 * read, never matches, and says why here.
 */
QString upscaleIdentityProblem(const UpscaleApplication &application);

/**
 * Why @p application's advertisement is never sent, or an empty string.
 *
 * The slot read at bind asks for an advertisement, which is made before any
 * window exists, when only an entry stating its program and no window can
 * answer. An entry that states a window, or no program, matches its windows
 * and applies everything else, but that one method never reaches the program.
 * Said in the editor rather than refused: the entry is valid, only incomplete.
 */
QString upscaleAdvertisementProblem(const UpscaleApplication &application);

/**
 * The application claiming a window with this identity, or null.
 *
 * Profiles are tried in their order and the first enabled one that matches
 * wins as a whole. An unresolved executable matches no gate 1, so every entry
 * stating one passes over such a window and only a gate-2-only entry can
 * claim it. This compiles nothing and reads nothing from disk; it runs when a
 * window's identity is resolved, never per frame.
 */
const UpscaleApplication *upscaleApplicationFor(const UpscaleIdentity &identity);

/** What the list says about a program before it has a window. */
struct UpscaleBindAnswer
{
    /** The profile the path alone selects, or null. */
    const UpscaleApplication *application = nullptr;
    /**
     * False when the path matches a profile that also states gate 2.
     *
     * That profile may or may not claim the window once it exists, so the
     * path alone does not decide, and an advertisement, which cannot be taken
     * back, is not made. Neither the profile nor the global profile answers:
     * the program is not unlisted, only undecided.
     */
    bool decided = true;
};

/**
 * The answer at wl_output bind, from the executable path alone.
 *
 * Only a profile stating gate 1 and no gate 2 can answer: one stating gate 2
 * cannot be known to match before the window exists, and a gate-2-only one
 * says nothing about paths at all, so it neither answers nor holds the
 * program back. A null application with decided set is an unlisted program,
 * for which the global profile answers.
 */
UpscaleBindAnswer upscaleApplicationAtBind(const QString &executable);

} // namespace KWin
