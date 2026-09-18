# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the scanned languages, the traced build and the report."""

import os
import runpy
import tempfile
import unittest
from collections import Counter
from pathlib import Path
from unittest import mock

SCANNER = runpy.run_path(str(Path(__file__).with_name("run-codeql.py")))
LANGUAGES = SCANNER["LANGUAGES"]
SUITES = SCANNER["SUITES"]
PATHS = SCANNER["PATHS"]
analyze = SCANNER["analyze"]
configuration = SCANNER["configuration"]
create = SCANNER["create"]
findings = SCANNER["findings"]
BUNDLE = SCANNER["BUNDLE"]
BUNDLE_SHA256 = SCANNER["BUNDLE_SHA256"]
BUNDLE_URL = SCANNER["BUNDLE_URL"]
install = SCANNER["install"]
installed = SCANNER["installed"]
executable = SCANNER["executable"]
existing = SCANNER["existing"]


class ExtractionTest(unittest.TestCase):
    """C++ needs a build to observe; the interpreted languages must not get one."""

    def test_languages(self) -> None:
        """Scan what GitHub reports for this repository, and nothing invented."""
        self.assertEqual(set(LANGUAGES), {"c-cpp", "python", "actions"})

    def test_cpp_uses_the_maintained_build(self) -> None:
        """Trace this project's own CMake and Ninja build, never autobuild."""
        command = create("codeql", "c-cpp", Path("out/db"), Path("out/build"), None)
        commands = [argument for argument in command if argument.startswith("--command=")]
        self.assertEqual(len(commands), 2)
        self.assertIn("-G Ninja", commands[0])
        self.assertIn("-B out/build", commands[0])
        self.assertEqual(commands[1], "--command=cmake --build out/build")
        self.assertNotIn("--build-mode=autobuild", command)

    def test_interpreted_languages_have_no_build(self) -> None:
        """A build command for Python or the workflows would only be a guess."""
        for language in ("python", "actions"):
            with self.subTest(language=language):
                command = create("codeql", language, Path("out/db"), Path("out/build"), None)
                self.assertFalse([item for item in command if item.startswith("--command=")])
                self.assertIn(f"--language={language}", command)

    def test_source_root_is_the_repository(self) -> None:
        """Extract the checkout, so SARIF locations name repository paths."""
        for language in LANGUAGES:
            with self.subTest(language=language):
                command = create("codeql", language, Path("d"), Path("b"), None)
                self.assertIn("--source-root=.", command)


class PathFilterTest(unittest.TestCase):
    """A scan of this repository must not become a scan of its own tools."""

    def test_interpreted_languages_are_confined(self) -> None:
        """Keep the unpacked bundle, the traced build and the caches out."""
        self.assertEqual(PATHS["python"], ("tools",))
        self.assertEqual(PATHS["actions"], (".github",))
        self.assertNotIn("c-cpp", PATHS)

    def test_filter_reaches_the_extractor(self) -> None:
        """Write the filter and hand it to database creation, or write none."""
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            written = configuration("python", output)
            if written is None:
                self.fail("Python must be confined to the directory that holds it")
            self.assertEqual(written.read_text(encoding="utf-8"), "paths:\n  - tools\n")
            command = create("codeql", "python", Path("d"), Path("b"), written)
            self.assertIn(f"--codescanning-config={written}", command)
            self.assertIsNone(configuration("c-cpp", output))
            cpp = create("codeql", "c-cpp", Path("d"), Path("b"), None)
            self.assertFalse([item for item in cpp if item.startswith("--codescanning-config=")])


class AnalysisTest(unittest.TestCase):
    """Each language keeps its own category and its own query pack."""

    def test_query_pack_names(self) -> None:
        """Use the pack name CodeQL publishes, which differs for C++."""
        packs = {
            language: analyze("codeql", language, Path("d"), Path("out.sarif"), "code-scanning")[4]
            for language in LANGUAGES
        }
        self.assertEqual(packs["c-cpp"], "codeql/cpp-queries:codeql-suites/cpp-code-scanning.qls")
        self.assertEqual(
            packs["python"], "codeql/python-queries:codeql-suites/python-code-scanning.qls"
        )
        self.assertEqual(
            packs["actions"], "codeql/actions-queries:codeql-suites/actions-code-scanning.qls"
        )

    def test_categories_are_distinct(self) -> None:
        """Without a category per language, each upload replaces the previous."""
        for language in LANGUAGES:
            with self.subTest(language=language):
                command = analyze("codeql", language, Path("d"), Path("s"), SUITES[0])
                self.assertIn(f"--sarif-category={language}", command)

    def test_default_suite_matches_github(self) -> None:
        """Start with the suite GitHub's default setup runs, not a wider one."""
        self.assertEqual(SUITES[0], "code-scanning")


class ReportTest(unittest.TestCase):
    """A scan that found nothing and a scan that was not read are not the same."""

    def test_empty_report(self) -> None:
        """Count nothing when there is nothing, without failing on the file."""
        self.assertEqual(findings({"runs": [{"tool": {"driver": {}}, "results": []}]}), Counter())

    def test_counts_by_severity(self) -> None:
        """Attribute each result to the severity its own rule declares."""
        report = {
            "runs": [
                {
                    "tool": {
                        "driver": {
                            "rules": [
                                {"id": "a", "properties": {"security-severity": "9.8"}},
                                {"id": "b", "properties": {"problem.severity": "warning"}},
                                {"id": "c", "properties": {}},
                            ]
                        }
                    },
                    "results": [
                        {"ruleId": "a"},
                        {"ruleId": "a"},
                        {"ruleId": "b"},
                        {"ruleId": "c"},
                        {"ruleId": "absent"},
                    ],
                }
            ]
        }
        self.assertEqual(findings(report), Counter({"9.8": 2, "warning": 1, "unknown": 2}))


class ExecutableTest(unittest.TestCase):
    """The scan must use a real command line, never report a run that was not."""

    def test_no_command_line_present(self) -> None:
        """Find nothing when nothing is installed, rather than a stale guess."""
        with (
            tempfile.TemporaryDirectory() as empty,
            mock.patch.dict(os.environ, {"PATH": empty}, clear=True),
        ):
            self.assertIsNone(existing(None))

    def test_distribution_variable(self) -> None:
        """CodeQL's own variable locates a bundle that is not on PATH."""
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory) / "codeql"
            bundle.write_text("", encoding="utf-8")
            bundle.chmod(0o755)
            environment = {"PATH": directory, "CODEQL_DIST": directory}
            with mock.patch.dict(os.environ, environment, clear=True):
                self.assertEqual(existing(None), str(bundle))
                self.assertEqual(executable(None, Path("unused")), str(bundle))

    def test_named_command_line_must_exist(self) -> None:
        """A path the caller named and got wrong is an error, not a download."""
        with (
            tempfile.TemporaryDirectory() as empty,
            mock.patch.dict(os.environ, {"PATH": empty}, clear=True),
            self.assertRaises(SystemExit) as refused,
        ):
            executable("codeql-that-is-not-installed", Path(empty))
        self.assertIn("not an executable", str(refused.exception))


class BundleTest(unittest.TestCase):
    """The command line is pinned like every other checker version here."""

    def test_pinned_release_download(self) -> None:
        """Fetch a named release asset from GitHub, never a moving latest."""
        self.assertTrue(
            BUNDLE_URL.startswith("https://github.com/github/codeql-action/releases/download/")
        )
        self.assertNotIn("latest", BUNDLE_URL)
        self.assertRegex(BUNDLE_SHA256, "^[0-9a-f]{64}$")

    def unpacked(self, root: Path, recorded: str | None) -> Path:
        """Lay out a bundle directory as a completed installation leaves it."""
        (root / "codeql").mkdir()
        (root / "codeql/codeql").write_text("", encoding="utf-8")
        if recorded is not None:
            (root / "pinned-bundle").write_text(recorded, encoding="utf-8")
        return root

    def test_matching_bundle_is_reused(self) -> None:
        """A bundle recorded as the current pin is what the scan may reuse."""
        with tempfile.TemporaryDirectory() as directory:
            root = self.unpacked(Path(directory), f"{BUNDLE} {BUNDLE_SHA256}\n")
            self.assertEqual(installed(root), f"{BUNDLE} {BUNDLE_SHA256}")
            self.assertEqual(install(root), root / "codeql")

    def test_bundle_from_another_pin_is_not_reused(self) -> None:
        """Raising the pin must not leave the scan on what an older one unpacked."""
        stale = "codeql-bundle-v1.0.0 " + "0" * 64
        for recorded in (f"{stale}\n", None):
            with self.subTest(recorded=recorded), tempfile.TemporaryDirectory() as directory:
                root = self.unpacked(Path(directory), recorded)
                self.assertNotEqual(installed(root), f"{BUNDLE} {BUNDLE_SHA256}")

    def test_incomplete_installation_is_not_reused(self) -> None:
        """A marker without a command line describes an extraction that failed."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "pinned-bundle").write_text(f"{BUNDLE} {BUNDLE_SHA256}\n", encoding="utf-8")
            self.assertEqual(installed(root), "")
