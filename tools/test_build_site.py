# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check that the site holds only the permanent documents and no dead link."""

import runpy
import tempfile
import unittest
from pathlib import Path

SCRIPT = runpy.run_path(str(Path(__file__).with_name("build-site.py")))
rewrite = SCRIPT["rewrite"]
build = SCRIPT["build"]
REPOSITORY = SCRIPT["REPOSITORY"]


class SiteTest(unittest.TestCase):
    """Links to held pages stay, links to anything else go to GitHub."""

    held = frozenset({"README.md", "doc/upscaling.md", "doc/checks.md"})

    def test_a_held_page_keeps_its_link(self) -> None:
        """A link between two pages of the site is left for Jekyll."""
        text = "see [checks](checks.md#containers)"
        self.assertEqual(rewrite(text, "doc/upscaling.md", set(self.held)), text)

    def test_an_agent_document_goes_to_github(self) -> None:
        """doc/agents/ is not on the site, and its anchors survive."""
        text = "[slice](agents/slice-resolution-control.md#the-bench-run-2026-09-29)"
        self.assertEqual(
            rewrite(text, "doc/upscaling.md", set(self.held)),
            f"[slice]({REPOSITORY}doc/agents/slice-resolution-control.md#the-bench-run-2026-09-29)",
        )

    def test_a_parent_path_resolves(self) -> None:
        """../AGENTS.md from doc/ is the repository's AGENTS.md."""
        self.assertEqual(
            rewrite("[rules](../AGENTS.md)", "doc/checks.md", set(self.held)),
            f"[rules]({REPOSITORY}AGENTS.md)",
        )

    def test_web_links_and_anchors_are_left(self) -> None:
        """An address with a scheme, or an anchor on the same page, is not touched."""
        text = "[KDE](https://kde.org) and [above](#start)"
        self.assertEqual(rewrite(text, "doc/upscaling.md", set(self.held)), text)

    def test_the_site_leaves_the_agent_documents_out(self) -> None:
        """What is written is the front page, the documents and the configuration."""
        with tempfile.TemporaryDirectory() as directory:
            written = build(Path(directory))
            names = {str(path.relative_to(directory)) for path in written}
            self.assertIn("index.md", names)
            self.assertIn("doc/upscaling.md", names)
            self.assertFalse(any("agents" in name or "AGENTS" in name for name in names))
            self.assertTrue((Path(directory) / "_config.yml").exists())


if __name__ == "__main__":
    unittest.main()
