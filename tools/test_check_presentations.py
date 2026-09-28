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


if __name__ == "__main__":
    unittest.main()
