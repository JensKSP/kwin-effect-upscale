# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for source budgets and comment handling."""

import runpy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

CHECKER = Path(__file__).with_name("check-file-size.py")
CODE_LINES = runpy.run_path(str(CHECKER))["code_lines"]


class FileSizeTest(unittest.TestCase):
    """Exercise the counter and its command line exit status."""

    def setUp(self) -> None:
        """Keep all fixtures outside the source tree."""
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def check_source(self, name: str, content: str | bytes) -> subprocess.CompletedProcess:
        """Write one fixture and run the same entry point as pre-commit."""
        path = self.root / name
        path.write_bytes(content.encode() if isinstance(content, str) else content)
        return subprocess.run(
            [sys.executable, str(CHECKER), str(path)],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_boundaries(self) -> None:
        """Warn above 300 and reject above 400 code lines."""
        for count, status, warning in (
            (300, 0, False),
            (301, 0, True),
            (400, 0, True),
            (401, 1, False),
        ):
            with self.subTest(lines=count):
                result = self.check_source("source.cpp", "int value;\n" * count)
                self.assertEqual(result.returncode, status)
                self.assertEqual("warning:" in result.stderr, warning)

    def test_source_kinds(self) -> None:
        """Shaders, templates and CMake files all obey the same limit."""
        for name, statement in (
            ("source.frag", "float value;"),
            ("source.cpp.in", "int value;"),
            ("CMakeLists.txt", "set(value 1)"),
            ("source.cmake", "set(value 1)"),
        ):
            with self.subTest(name=name):
                self.assertEqual(self.check_source(name, (statement + "\n") * 401).returncode, 1)

    def test_comments(self) -> None:
        """Documentation and blank lines do not consume the code budget."""
        content = "/*\n" + "documentation\n" * 550 + "*/\n\nint value; // comment\n"
        self.assertEqual(self.check_source("source.cpp", content).returncode, 0)
        self.assertEqual(CODE_LINES(self.root / "source.cpp"), 1)

    def test_raw_string(self) -> None:
        """Embedded shaders cannot disappear into one counted string line."""
        content = 'const char *shader = R"glsl(\n' + "float value;\n" * 500 + ')glsl";\n'
        self.assertEqual(self.check_source("source.cpp", content).returncode, 1)
        self.assertEqual(CODE_LINES(self.root / "source.cpp"), 502)

    def test_continued_string(self) -> None:
        """Escaped line breaks in ordinary literals retain their code lines."""
        content = 'const char *text = "' + "value\\\n" * 500 + '";\n'
        self.assertEqual(self.check_source("source.cpp", content).returncode, 1)

    def test_python_strings(self) -> None:
        """Exclude actual docstrings, but keep assigned triple-quoted data."""
        docstring = '"""\n' + "documentation\n" * 500 + '"""\n'
        result = self.check_source("source.py", docstring + "value = 1\n")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(CODE_LINES(self.root / "source.py"), 1)
        self.assertEqual(self.check_source("source.py", "value = " + docstring).returncode, 1)

    def test_python_inline_docstring(self) -> None:
        """Preserve code following a docstring, including UTF-8 identifiers."""
        content = 'def größe(): """Dokumentation"""; return "# data"\n'
        self.assertEqual(self.check_source("source.py", content).returncode, 0)
        self.assertEqual(CODE_LINES(self.root / "source.py"), 1)

    def test_hash_languages(self) -> None:
        """A `#` in a string or a parameter expansion does not open a comment."""
        shell = '# comment\nn=$#\necho "${#n} items # not a comment"\necho hi   # real\n'
        self.check_source("source.sh", shell)
        self.assertEqual(CODE_LINES(self.root / "source.sh"), 3)

        cmake = '# comment\n#[[ bracket\ncomment ]]\nset(x "a#b")\nadd_subdirectory(y)\n'
        self.check_source("CMakeLists.txt", cmake)
        self.assertEqual(CODE_LINES(self.root / "CMakeLists.txt"), 2)

    def test_unknown_language(self) -> None:
        """A file with no counting rule is an error, never a silent pass."""
        result = self.check_source("source.rs", "fn main() {}\n")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no line counting rule", result.stderr)

    def test_invalid_input(self) -> None:
        """Reject unreadable input and syntax that cannot be counted safely."""
        for name, content in (("bad.cpp", b"// \xff\n"), ("bad.py", "def broken(:\n")):
            with self.subTest(name=name):
                result = self.check_source(name, content)
                self.assertEqual(result.returncode, 1)
                self.assertIn("cannot measure", result.stderr)
        result = subprocess.run(
            [sys.executable, str(CHECKER), str(self.root / "missing.cpp")],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("cannot measure", result.stderr)
