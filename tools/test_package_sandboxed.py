# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the Flatpak and Snap check and how package-vm.py runs it."""

import json
import runpy
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

import package_check
import package_sandboxed as ps

PACKAGE_VM = runpy.run_path(str(Path(__file__).with_name("package-vm.py")))


class SandboxedTest(unittest.TestCase):
    """Read KWin's and the proxy's answers, and find a snap's program."""

    def test_finds_windows_by_their_kwin_ids(self) -> None:
        """Take the ID after the runner's action, which the effect asks by."""
        uuid = "{0c1f2d0e-5a8b-4c55-9d3a-1b2c3d4e5f60}"
        answer = {
            "type": "a(sssida{sv})",
            "data": [[[f"0_{uuid}", "SuperTuxKart", "", 100, 1.0, {}]]],
        }
        with mock.patch.object(package_check, "output", return_value=json.dumps(answer)) as asked:
            self.assertEqual(ps.window_ids("supertuxkart"), [uuid])
        self.assertIn("window appname=supertuxkart", asked.call_args.args[0])

    def test_finds_the_proxys_answer_for_the_entry_alone(self) -> None:
        """Take only a connection told the smaller screen for the entry asked about."""
        lines = [
            'Upscale X11 connection pid= 41 profile= "" size= QSize(-1, -1) reason= "no entry"',
            (
                'Upscale X11 connection pid= 42 profile= "extremetuxracer" size= QSize(2560, 1440) '
                'reason= "connection display advertisement" names= ("flatpak://net.sourceforge.'
                'ExtremeTuxRacer/app/bin/etr", "/app/bin/etr")'
            ),
        ]
        with mock.patch.object(package_check, "journal", return_value="\n".join(lines)):
            self.assertEqual(ps.answer_for("extremetuxracer", "now"), lines[1])
            self.assertEqual(ps.answer_for("supertuxkart", "now"), "")

    def test_names_a_snaps_own_program_first(self) -> None:
        """Prefer the program named as the snap, then its first application."""
        with tempfile.TemporaryDirectory() as scratch:
            directory = Path(scratch)
            (directory / "extreme-tux-racer.etr").touch()
            self.assertEqual(
                ps.snap_program("extreme-tux-racer", directory),
                str(directory / "extreme-tux-racer.etr"),
            )
            (directory / "extreme-tux-racer").touch()
            self.assertEqual(
                ps.snap_program("extreme-tux-racer", directory),
                str(directory / "extreme-tux-racer"),
            )

    def test_every_sandbox_runs_both_routes(self) -> None:
        """Run SuperTuxKart on Wayland and through X11 and the racer through X11, in each."""
        for cases in (ps.flatpak_cases(), ps.snap_cases()):
            routes = [(case.profile, case.x11) for case in cases]
            self.assertEqual(
                routes,
                [("supertuxkart", False), ("supertuxkart", True), ("extremetuxracer", True)],
            )


class SessionTest(unittest.TestCase):
    """Run a guest check by its script, and name its report."""

    def run_session(self, command: tuple[str, ...]) -> list[str]:
        """Run session() with nothing reaching a machine, and answer the guest's command."""
        machine = mock.Mock(shared="/src/build/machine", directory=Path(tempfile.mkdtemp()))
        package = machine.directory / "game.deb"
        package.touch()
        session = PACKAGE_VM["session"]
        with (
            mock.patch.dict(session.__globals__, {"vm": mock.Mock()}),
            mock.patch.dict(session.__globals__, {"subprocess": mock.Mock()}),
            mock.patch.dict(session.__globals__, {"shutil": mock.Mock()}),
        ):
            session(machine, command, {"--package": package})
            return list(session.__globals__["vm"].ssh.call_args.args[1:])

    def test_runs_a_subcommand_of_the_session_script(self) -> None:
        """Keep the subcommand and name the report after it."""
        guest = self.run_session(("package_session.py", "languages"))
        self.assertEqual(guest[3:5], ["/src/tools/package_session.py", "languages"])
        self.assertEqual(guest[-1], "/src/build/machine/languages.json")

    def test_runs_a_script_without_a_subcommand(self) -> None:
        """Pass the options straight to a script of its own."""
        guest = self.run_session(("package_sandboxed.py",))
        self.assertEqual(guest[3:5], ["/src/tools/package_sandboxed.py", "--package"])
        self.assertEqual(guest[-1], "/src/build/machine/package_sandboxed.json")


if __name__ == "__main__":
    unittest.main()
