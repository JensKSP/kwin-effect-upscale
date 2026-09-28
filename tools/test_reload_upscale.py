# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check replacement, cleanup and recovery without touching a real compositor."""

import runpy
import shutil
import tempfile
import unittest
from functools import partial
from pathlib import Path
from unittest import mock

MODULE = runpy.run_path(str(Path(__file__).with_name("reload-upscale.py")))
reload_plugin = MODULE["reload_plugin"]


def privileged(arguments: list[str], *, gui: bool, failure: str) -> None:
    """Stage only disposable test files, with optional authentication failure."""
    if gui:
        message = "A unit test must not request a desktop dialog."
        raise RuntimeError(message)
    if "0644" in arguments:
        if failure == "authentication":
            message = "Authentication cancelled"
            raise RuntimeError(message)
        shutil.copyfile(arguments[-2], arguments[-1])
    else:
        Path(arguments[-1]).unlink()


class ReloadTest(unittest.TestCase):
    """A failed reload must not masquerade as a successful new build."""

    def exercise(self, failure: str = "") -> tuple[set[str], list[str]]:
        """Simulate KWin and filesystem operations, returning their final state."""
        active = {"upscale", "blur"}
        loaded = []

        def bus(method: str, *arguments: str) -> str:
            if method == "loadedEffects":
                return "\n".join(sorted(active))
            name = arguments[0]
            if method == "unloadEffect":
                active.discard(name)
                return ""
            if method == "loadEffect":
                loaded.append(name)
                if failure == "load" and name != "upscale":
                    return "false"
                active.add(name)
                return "true"
            if failure == "identity":
                return "status: no build identity"
            return f"{name}:\nbuild: expected fresh build\nstatus: inactive"

        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "upscale.so"
            source.write_bytes(b"unchanged installed binary")
            with mock.patch.dict(
                reload_plugin.__globals__,
                {"bus": bus, "privileged": partial(privileged, failure=failure)},
            ):
                if failure:
                    with self.assertRaises(RuntimeError):
                        reload_plugin(source, gui=False)
                else:
                    status = reload_plugin(source, gui=False)
                    self.assertIn("build: expected fresh build", status)
            self.assertEqual(list(Path(directory).iterdir()), [source])
            self.assertEqual(source.read_bytes(), b"unchanged installed binary")
        return active, loaded

    def test_fresh_copy_replaces_only_upscale_and_is_removed(self) -> None:
        """Leave one fresh effect active and no extra discoverable plugin file."""
        active, loaded = self.exercise()
        self.assertEqual(len(loaded), 1)
        self.assertTrue(loaded[0].startswith("upscale_reload_"))
        self.assertEqual(active, {"blur", loaded[0]})

    def test_cancelled_authentication_leaves_existing_effect(self) -> None:
        """Do not unload until the replacement is available."""
        active, loaded = self.exercise("authentication")
        self.assertEqual(active, {"blur", "upscale"})
        self.assertEqual(loaded, [])

    def test_refused_load_recovers_regular_effect(self) -> None:
        """Report failure even if the old library can be restored."""
        active, loaded = self.exercise("load")
        self.assertEqual(active, {"blur", "upscale"})
        self.assertEqual(loaded[-1], "upscale")

    def test_missing_build_identity_is_not_success(self) -> None:
        """Remove an unverifiable fresh instance before recovering."""
        active, loaded = self.exercise("identity")
        self.assertEqual(active, {"blur", "upscale"})
        self.assertEqual(loaded[-1], "upscale")


if __name__ == "__main__":
    unittest.main()
