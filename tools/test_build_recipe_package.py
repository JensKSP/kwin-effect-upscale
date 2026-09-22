# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the FreeBSD package's packing list against pkg's own path rule, without FreeBSD."""

import re
import runpy
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

BUILD = Path(__file__).with_name("build-recipe-package.py").resolve()
ROOT = BUILD.parent.parent


class FreeBsdPackageTest(unittest.TestCase):
    """The first nightly with a FreeBSD package failed where this now looks."""

    def test_every_listed_file_is_where_pkg_create_looks(self) -> None:
        """Each packing list entry is read at root directory + manifest prefix + entry."""
        module = runpy.run_path(str(BUILD))
        with tempfile.TemporaryDirectory() as temporary:
            work = Path(temporary) / "freebsd"
            created = []

            def run(command: list[str], **options: object) -> subprocess.CompletedProcess[str]:
                if command[:2] == ["cmake", "--install"]:
                    environment = options["env"]
                    if not isinstance(environment, dict):
                        self.fail("cmake --install was not given an environment")
                    staged = Path(environment["DESTDIR"]) / "usr/local/etc/xdg/kwinupscalerc"
                    staged.parent.mkdir(parents=True)
                    staged.write_text("")
                if command[:2] == ["pkg", "create"]:
                    manifest = Path(command[command.index("-M") + 1]).read_text()
                    prefix = re.search(r"^prefix:\s*(\S+)", manifest, re.MULTILINE)
                    if prefix is None:
                        self.fail("the manifest states no prefix")
                    root = Path(command[command.index("-r") + 1])
                    for entry in Path(command[command.index("-p") + 1]).read_text().split():
                        self.assertTrue(
                            (root / prefix.group(1).lstrip("/") / entry).is_file(), entry
                        )
                    package = work / "kwin-effect-upscale-0.1.0.pkg"
                    package.write_text("")
                    created.append(package)
                return subprocess.CompletedProcess(command, 0)

            with (
                mock.patch.object(subprocess, "run", run),
                mock.patch.object(subprocess, "check_output", return_value="x11-wm/plasma6-kwin\n"),
            ):
                packages = module["build_pkg"](ROOT, work, "0.1.0", "6.3.6")
            self.assertEqual(len(created), 1)
            self.assertEqual(len(packages), 1)
            self.assertTrue(packages[0].name.startswith("kwin-effect-upscale-0.1.0-"))


if __name__ == "__main__":
    unittest.main()
