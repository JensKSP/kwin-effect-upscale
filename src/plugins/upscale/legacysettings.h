/*
    SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <optional>

#include "settings.h"

class KConfigGroup;

namespace KWin
{

struct UpscaleApplication;

/**
 * What the previous release stored in the global group, read under the
 * meaning it had then.
 *
 * Nothing here writes. The compositor reads configuration and never rewrites
 * a person's files on its own, so an old key is translated every time it is
 * read for as long as no new key has replaced it. The settings page is what
 * replaces it, on Apply, through upscaleForgetLegacySettings(); until then both
 * the effect and the page see the same translated value.
 *
 * This file is temporary by nature. Once no installation can still carry the
 * old keys it can be deleted whole, which is why none of it is spread through
 * the code that reads the current ones.
 */

/**
 * The global resolution as the old Preset key stated it, if it did.
 *
 * The preset enumeration lost Automatic, its first value, so every stored
 * number after it names the preset one below it now. Automatic itself becomes
 * no value at all: it meant that nobody had chosen, which is what an absent
 * key means now, and translating it to Native instead would stop the effect
 * reducing the very games it ships profiles for.
 */
std::optional<int> upscaleLegacyResolution(const KConfigGroup &global);

/**
 * Whether the old UnknownApplications key asked for unlisted applications.
 *
 * That setting is now two things: the global profile acting on windows no
 * profile claimed, and the global profile's own method, which is what said
 * anything to such a program. Where it was on, both follow.
 */
bool upscaleLegacyUnlisted(const KConfigGroup &global);

/**
 * Whether the old Enabled key had switched upscaling off entirely.
 *
 * Nothing in the new model says that in one place: participation is per
 * profile, and the shipped profiles are on. Reading an old off as anything
 * else would switch upscaling on for someone who had turned it off, so it is
 * honoured as it was meant until a person changes it deliberately.
 */
bool upscaleLegacySwitchedOff(const KConfigGroup &global);

/**
 * Remove the old keys whose meaning the new ones now carry in full.
 *
 * Called when the settings page applies, after it has written the new keys.
 * Enabled is deliberately kept: no new key carries "everything off", so
 * removing it would be the page silently switching upscaling back on.
 */
void upscaleForgetLegacySettings(KConfigGroup &global);

/**
 * A profile's own old keys, read into @p overrides under the meaning they had.
 *
 * Two of them. A profile's Preset stored a preset's name, which reads as its
 * Resolution; Automatic, which meant "follow the global one", becomes no
 * value, which means exactly that now. And MinimumPixels stored -1 to mean
 * "inherit the global threshold": read as a number it would clamp to zero,
 * which disables the threshold and scales on every output, the opposite of
 * what it said. So -1 reads as absent.
 */
void upscaleReadLegacyOverrides(const KConfigGroup &profile, UpscaleSettingOverrides &overrides);

/**
 * A profile's old Program key, read into @p application as the gate it meant.
 *
 * Program named a file and was compared by file name alone, and it was only
 * ever consulted at wl_output bind, to decide an advertisement; windows were
 * found by their class and instance. The two-gate model has no field that
 * means "at bind only", so each old entry is read as the nearest thing that
 * behaves the same:
 *
 * - With no window identity, the program was all there was: it becomes gate 1,
 *   the file name in any directory.
 * - With a window identity and an advertisement in the slot that is read at
 *   bind, the program was how that advertisement was found. It becomes gate 1
 *   and the window identity is left out, because an entry that also required
 *   a window could not advertise before the window exists. For a native
 *   Wayland client the path reaches the same windows: its window and its
 *   connection are the same program.
 * - Otherwise it decided nothing - an X11 window was found by its identity,
 *   and nothing but an advertisement is said at bind - and it is dropped.
 *
 * Nothing is read where the profile already states an Executable.
 */
void upscaleReadLegacyProgram(const KConfigGroup &profile, UpscaleApplication &application);

/**
 * Replace a profile's old keys with the current ones, keeping what they said.
 *
 * Called when the editor saves a profile. The values the old keys held have
 * already been read into @p application, but the editor writes only what a
 * person changed, so an unchanged value exists nowhere except under the old
 * key. Deleting that key alone would therefore drop a measured method or a
 * chosen resolution on the first save after an upgrade; this writes each one
 * under its current key first, and removes the old key after.
 */
void upscaleRetireLegacyProfileKeys(KConfigGroup &profile, const UpscaleApplication &application);

} // namespace KWin
