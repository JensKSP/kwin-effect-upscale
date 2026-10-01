# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fixture adaptation must preserve assertions and observe unexpected unloads."""

import tempfile
import unittest
from pathlib import Path

from kwin_conformance_fixtures import FIXTURES, adapt_fixtures


class FixtureTest(unittest.TestCase):
    """Exercise both upstream cleanup forms without needing a KWin checkout."""

    def setUp(self) -> None:
        """Create representative fixtures with teardown outside the unload scope."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.originals: dict[str, str] = {}
        for relative, name in FIXTURES.items():
            unload = "    effects->unloadAllEffects();"
            if name == "SlidingPopupsTest":
                unload = (
                    "    while (!effects->loadedEffects().isEmpty()) {\n"
                    "        const QString effect = effects->loadedEffects().first();\n"
                    "        effects->unloadEffect(effect);\n"
                    "        QVERIFY(!effects->isEffectLoaded(effect));\n"
                    "    }"
                )
            source = (
                '#include "kwin_wayland_test.h"\n'
                f"void {name}::init()\n{{\n    setupClient();\n}}\n"
                f"void {name}::cleanup()\n{{\n    destroyClient();\n"
                f"{unload}\n    verifyTeardown();\n}}\n"
                f"void {name}::testWindow()\n{{\n"
                "    effects->unloadAllEffects();\n    QCOMPARE(actual, expected);\n}\n"
            )
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(source)
            self.originals[relative] = source

    def test_only_explicit_fixture_unloads_are_guarded(self) -> None:
        """Client teardown and test bodies must still report unexpected destruction."""
        adapt_fixtures(self.root)
        for relative, name in FIXTURES.items():
            with self.subTest(fixture=relative):
                source = (self.root / relative).read_text()
                body = f"void {name}::testWindow()"
                self.assertEqual(source.split(body)[1], self.originals[relative].split(body)[1])
                self.assertIn("loadUpscaleConformance();\n    setupClient();", source)
                self.assertIn("destroyClient();\n    {\n", source)
                self.assertIn("\n    }\n    verifyTeardown();", source)
                self.assertEqual(source.count("s_upscaleConformanceFixtureUnloading, true"), 1)

    def test_refresh_is_idempotent(self) -> None:
        """Refreshing the private copy must not nest guards or duplicate loading."""
        adapt_fixtures(self.root)
        before = {relative: (self.root / relative).read_bytes() for relative in FIXTURES}
        adapt_fixtures(self.root)
        self.assertEqual(before, {path: (self.root / path).read_bytes() for path in FIXTURES})

    def test_changed_upstream_cleanup_is_rejected(self) -> None:
        """Never silently leave a changed upstream cleanup uninstrumented."""
        relative = next(iter(FIXTURES))
        path = self.root / relative
        path.write_text(self.originals[relative].replace("unloadAllEffects", "unloadChanged"))
        with self.assertRaises(ValueError):
            adapt_fixtures(self.root)

    def test_refresh_rejects_an_extra_unguarded_unload(self) -> None:
        """An existing guard must not hide a newly added cleanup operation."""
        adapt_fixtures(self.root)
        path = self.root / next(iter(FIXTURES))
        path.write_text(
            path.read_text().replace(
                "    verifyTeardown();", "    effects->unloadAllEffects();\n    verifyTeardown();"
            )
        )
        with self.assertRaises(ValueError):
            adapt_fixtures(self.root)
