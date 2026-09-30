# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Adapt the explicit effect-unloading fixtures in a private upstream copy."""

import re
import textwrap
from pathlib import Path

FIXTURES = {
    "dont_crash_reinitialize_compositor.cpp": "DontCrashReinitializeCompositorTest",
    "effects/slidingpopups_test.cpp": "SlidingPopupsTest",
    "effects/wobbly_shade_test.cpp": "WobblyWindowsShadeTest",
    "effects/toplevel_open_close_animation_test.cpp": "ToplevelOpenCloseAnimationTest",
    "effects/scripted_effects_test.cpp": "ScriptedEffectsTest",
    "effects/popup_open_close_animation_test.cpp": "PopupOpenCloseAnimationTest",
    "effects/desktop_switching_animation_test.cpp": "DesktopSwitchingAnimationTest",
    "effects/minimize_animation_test.cpp": "MinimizeAnimationTest",
    "effects/maximize_animation_test.cpp": "MaximizeAnimationTest",
}


def replace_fixture(source: str, old: str, new: str) -> str:
    """Refresh idempotently, rejecting a changed upstream fixture."""
    if source.count(new) == 1 and old not in source.replace(new, ""):
        return source
    if new in source or source.count(old) != 1:
        message = f"expected exactly one fixture location: {old!r}"
        raise ValueError(message)
    return source.replace(old, new)


def adapt_fixtures(integration: Path) -> None:
    """Reload per row; tolerate destruction only inside explicit fixture unloads."""
    for relative, name in FIXTURES.items():
        path = integration / relative
        source = path.read_text()
        source = replace_fixture(
            source,
            '#include "kwin_wayland_test.h"',
            '#include "kwin_wayland_test.h"\n#include "kwin_conformance.h"',
        )
        init = f"void {name}::init()\n{{\n"
        source = replace_fixture(source, init, init + "    loadUpscaleConformance();\n")
        pattern = rf"(void {name}::cleanup\(\)\n\{{\n)(.*?)(^\}})"
        matches = list(re.finditer(pattern, source, re.MULTILINE | re.DOTALL))
        if len(matches) != 1:
            message = f"expected exactly one cleanup fixture: {path}"
            raise ValueError(message)
        match = matches[0]
        unload = "    effects->unloadAllEffects();"
        if name == "SlidingPopupsTest":
            unload = (
                "    while (!effects->loadedEffects().isEmpty()) {\n"
                "        const QString effect = effects->loadedEffects().first();\n"
                "        effects->unloadEffect(effect);\n"
                "        QVERIFY(!effects->isEffectLoaded(effect));\n"
                "    }"
            )
        guarded = (
            "    {\n"
            "        const QScopedValueRollback<bool> unloading("
            "s_upscaleConformanceFixtureUnloading, true);\n"
            + textwrap.indent(unload, "    ")
            + "\n    }"
        )
        body = replace_fixture(match[2], unload, guarded)
        source = source[: match.start(2)] + body + source[match.end(2) :]
        if source != path.read_text():
            path.write_text(source)
