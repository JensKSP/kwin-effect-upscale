# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for what the presentation check counts as a failure."""

import contextlib
import io
import runpy
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HARNESS = runpy.run_path(str(Path(__file__).with_name("check-presentations.py")))


def absent(case: object, *_: object) -> dict[str, object]:
    """Answer a case the way a machine without its game does."""
    return {
        "game": getattr(case, "game", ""),
        "presentation": getattr(case, "presentation", ""),
        "outcome": "absent",
        "detail": "the game is not installed",
    }


class PresentationCheckTest(unittest.TestCase):
    """A requested case is judged, never passed over."""

    def run_main(self, *arguments: str) -> tuple[int, list[object]]:
        """Run the command line with a fake case runner, and return the cases."""
        cases: list[object] = []

        def run_case(case: object, *rest: object) -> dict[str, object]:
            cases.append(case)
            return absent(case, *rest)

        main = HARNESS["main"]
        with (
            tempfile.TemporaryDirectory() as build,
            mock.patch.dict(main.__globals__, {"run_case": run_case}),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            (Path(build) / "bin").mkdir()
            outcome = main(["--build", build, "--window", "game", *arguments, "--", "game"])
        return outcome, cases

    def test_a_case_whose_game_is_missing_fails(self) -> None:
        """Exit status 0 would claim a game ran that never started."""
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(HARNESS["report"]([absent(None)]), 1)

    def test_a_windowed_window_is_expected_to_be_left_alone(self) -> None:
        """The effect never acts on a windowed window, so neither does the default."""
        for presentation, acted in (
            ("windowed", False),
            ("fullscreen", True),
            ("borderless", True),
        ):
            with self.subTest(presentation=presentation):
                _, cases = self.run_main("--presentation", presentation)
                self.assertEqual(getattr(cases[0], "acted", None), acted)

    def test_an_explicit_expectation_wins_over_the_default(self) -> None:
        """A fullscreen case can still say the effect should leave it alone."""
        _, cases = self.run_main("--presentation", "fullscreen", "--no-acted")
        self.assertFalse(getattr(cases[0], "acted", True))

    def test_the_case_declares_its_game_a_game(self) -> None:
        """All games acts only for a game, so the case's program is declared one."""
        with tempfile.TemporaryDirectory() as root:
            program = Path(root) / "bin" / "game"
            program.parent.mkdir()
            program.write_text("")
            self.assertEqual(HARNESS["declare_game"](Path(root), str(program)), "")
            entry = (Path(root) / "data" / "applications" / "upscale-case.desktop").read_text()
        self.assertIn("\nCategories=Game;\n", entry)
        self.assertIn(f'\nExec="{program.resolve()}"\n', entry)

    def test_a_path_the_entry_cannot_name_is_refused(self) -> None:
        """An Exec line that names another program would declare the wrong game."""
        with tempfile.TemporaryDirectory() as root:
            problem = HARNESS["declare_game"](Path(root), str(Path(root) / 'a"game'))
            self.assertIn("double quote", problem)
            self.assertFalse((Path(root) / "data").exists())


if __name__ == "__main__":
    unittest.main()
