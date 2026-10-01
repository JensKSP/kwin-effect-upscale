#! /usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: CC0-1.0
#
# The translation template, as KDE's translation scripts make it: they set
# XGETTEXT and podir and run this from the repository root. The catalogues live
# in po/<language>/, and tools/check-translations.py extracts the same template
# to keep them complete.
$XGETTEXT $(find src -name '*.cpp' -o -name '*.h' | sort) -o "$podir/kwin_effect_upscale.pot"
