# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that a download is tried again only where the package manager fails at once."""

import runpy
import subprocess
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = runpy.run_path(str(Path(__file__).with_name("test-package.py")))
MANAGERS = SCRIPT["MANAGERS"]
fetch = SCRIPT["fetch"]


def failing(times: int) -> mock.Mock:
    """Stand in for a command that fails the given number of times, then succeeds."""
    outcomes = [subprocess.CalledProcessError(4, "zypper")] * times
    return mock.Mock(side_effect=[*outcomes, None])


class FetchTest(unittest.TestCase):
    """openSUSE tries four times, with a growing pause; the others once."""

    def test_opensuse_tries_again(self) -> None:
        """A refused download that succeeds on the third try passes."""
        command = failing(2)
        with (
            mock.patch.dict(fetch.__globals__, {"run": command}),
            mock.patch("time.sleep") as sleep,
        ):
            fetch(MANAGERS["opensuse"], "zypper", "install", pause=1)
        self.assertEqual(command.call_count, 3)
        self.assertEqual([call.args[0] for call in sleep.call_args_list], [1, 2])

    def test_opensuse_gives_up_after_four_tries(self) -> None:
        """A server that keeps refusing still fails the check."""
        command = failing(4)
        with (
            mock.patch.dict(fetch.__globals__, {"run": command}),
            mock.patch("time.sleep"),
            self.assertRaises(subprocess.CalledProcessError),
        ):
            fetch(MANAGERS["opensuse"], "zypper", "install", pause=1)
        self.assertEqual(command.call_count, 4)

    def test_a_manager_that_retries_itself_is_run_once(self) -> None:
        """A failure of dnf is final here: it already tried again."""
        command = failing(1)
        with (
            mock.patch.dict(fetch.__globals__, {"run": command}),
            mock.patch("time.sleep") as sleep,
            self.assertRaises(subprocess.CalledProcessError),
        ):
            fetch(MANAGERS["fedora"], "dnf", "install", pause=1)
        self.assertEqual(command.call_count, 1)
        sleep.assert_not_called()


if __name__ == "__main__":
    unittest.main()
