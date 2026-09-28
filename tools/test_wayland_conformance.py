# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""A comparison cannot pass merely because the effect or its tests were absent."""

import runpy
import tempfile
import unittest
from pathlib import Path

CHECK = runpy.run_path(str(Path(__file__).with_name("check-wayland-conformance.py")))
RESULT = CHECK["Result"]
COMPARE = CHECK["compare"]
READ_CASES = CHECK["read_cases"]


class VerdictTest(unittest.TestCase):
    """Exercise the failures that made the earlier VM comparison inconclusive."""

    def test_unloaded_effect_is_inconclusive(self) -> None:
        """Identical passes do not compensate for an effect that never loaded."""
        cases = {"initTestCase": "pass", "window": "pass", "cleanupTestCase": "pass"}
        absent = RESULT(
            "suite", "absent", 0, engaged=True, scaled=False, complete=True, cases=cases
        )
        idle = RESULT("suite", "idle", 0, engaged=False, scaled=False, complete=True, cases=cases)
        self.assertIn("idle/suite", COMPARE([absent, idle])["invalid"])
        self.assertIn("active/suite: missing arm", COMPARE([absent, idle])["invalid"])

    def test_missing_row_and_new_failure_are_not_passes(self) -> None:
        """Retain new skips and missing rows as well as ordinary failures."""
        absent = RESULT(
            "suite",
            "absent",
            0,
            engaged=True,
            scaled=False,
            complete=True,
            cases={"unchanged": "pass", "changed": "pass", "missing": "pass"},
        )
        for outcome in ("fail", "skip", "xfail"):
            idle = RESULT(
                "suite",
                "idle",
                1,
                engaged=True,
                scaled=False,
                complete=True,
                cases={"unchanged": "pass", "changed": outcome},
            )
            verdict = COMPARE([absent, idle])
            self.assertEqual(len(verdict["idle_regressions"]), 3)

    def test_scaling_requires_a_completed_draw(self) -> None:
        """Successful request tests cannot masquerade as successful scaling."""
        result = RESULT(
            "kwin-testUpscaleProduction",
            "active",
            0,
            engaged=True,
            scaled=False,
            complete=True,
            cases={"request": "pass"},
        )
        self.assertTrue(COMPARE([result])["invalid"])
        result.scaled = True
        self.assertFalse(COMPARE([result])["invalid"])
        result.cases["pixels"] = "skip"
        self.assertTrue(COMPARE([result])["invalid"])

    def test_truncated_xml_is_incomplete(self) -> None:
        """A crash after one passing assertion is not a completed run."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.xml"
            path.write_text('<TestCase><TestFunction name="first"><Incident type="pass"/>')
            self.assertEqual(READ_CASES(path), ({}, False))

    def test_unloaded_during_suite_is_inconclusive(self) -> None:
        """Loading once cannot validate rows that unload the effect themselves."""
        result = RESULT(
            "suite",
            "idle",
            0,
            engaged=True,
            scaled=False,
            complete=True,
            cases={"window": "pass"},
            effect_lost=True,
        )
        self.assertFalse(CHECK["exercised"](result))

    def test_data_rows_remain_distinct(self) -> None:
        """A passing data row must not overwrite the failing row beside it."""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "result.xml"
            path.write_text(
                '<TestCase><TestFunction name="scale">'
                '<Incident type="fail"><DataTag>1.5</DataTag></Incident>'
                '<Incident type="pass"><DataTag>3</DataTag></Incident>'
                '<Message type="skip"><DataTag>4</DataTag></Message>'
                '</TestFunction><TestFunction name="cleanupTestCase">'
                '<Incident type="pass"/></TestFunction></TestCase>'
            )
            cases, complete = READ_CASES(path)
            self.assertTrue(complete)
            self.assertEqual(cases["scale:1.5"], "fail")
            self.assertEqual(cases["scale:3"], "pass")
            self.assertEqual(cases["scale:4"], "skip")

    def test_exclusions_are_named_reported_and_bounded(self) -> None:
        """A case excluded by name is reported, only in its arms, and hides nothing else."""
        excluded = "kwin-testScreenChanges/testScreenAddRemove"
        name, case = excluded.split("/")
        absent = RESULT(
            name,
            "absent",
            0,
            engaged=True,
            scaled=False,
            complete=True,
            cases={case: "pass", "other": "pass"},
        )
        # The mode advertisement is excluded where the effect acts, not where it idles.
        idle = RESULT(
            name,
            "idle",
            1,
            engaged=True,
            scaled=False,
            complete=True,
            cases={case: "fail", "other": "pass"},
        )
        active = RESULT(
            name,
            "active",
            1,
            engaged=True,
            scaled=False,
            complete=True,
            cases={case: "fail", "other": "pass"},
        )
        verdict = COMPARE([absent, idle, active])
        self.assertEqual(len(verdict["idle_regressions"]), 2)
        self.assertEqual(verdict["active_differences"], [])
        self.assertEqual(len(verdict["excluded"]), 2)
        self.assertTrue(all(line.startswith("active: ") for line in verdict["excluded"]))
        # Another change in the same executable keeps its exit code a difference.
        active.cases["other"] = "fail"
        verdict = COMPARE([absent, idle, active])
        self.assertEqual(
            verdict["active_differences"],
            [f"{name}/other: pass -> fail", f"{name}/exit: 0 -> 1"],
        )
        self.assertEqual(len(verdict["excluded"]), 1)

    def test_a_run_the_baseline_cannot_finish_is_the_systems(self) -> None:
        """A suite that crashes without the effect is recorded, not held against it."""
        crashed = {"initTestCase": "pass"}
        results = [
            RESULT("suite", arm, -6, engaged=True, scaled=False, complete=False, cases=crashed)
            for arm in ("absent", "idle", "active")
        ]
        verdict = COMPARE(results)
        self.assertEqual(verdict["invalid"], [])
        self.assertEqual(len(verdict["baseline_invalid"]), 3)
        # The same crash only where the effect is loaded stays invalid.
        results[0] = RESULT(
            "suite", "absent", 0, engaged=True, scaled=False, complete=True, cases=crashed
        )
        verdict = COMPARE(results)
        self.assertEqual(sorted(verdict["invalid"]), ["active/suite", "idle/suite"])
        self.assertEqual(verdict["baseline_invalid"], [])
