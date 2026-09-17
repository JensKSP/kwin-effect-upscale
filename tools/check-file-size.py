#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""Fail when a source file grows past its budget of code lines.

A file is measured by what a reader has to hold in their head: comments, blank
lines and the licence header do not count, so documenting a function never
pushes a file over the limit. KDE has no rule of this kind, so the numbers are
this project's own and live only here.

The budget is per file. Its counterpart is readability-function-size in
.clang-tidy, because a file can sit at 399 lines and still hold one function
nobody can follow.
"""

from __future__ import annotations

import argparse
import ast
import io
import sys
import tokenize
from pathlib import Path

WARN_DEFAULT = 300
FAIL_DEFAULT = 400

CXX_SUFFIXES = {
    ".c",
    ".cc",
    ".cpp",
    ".cxx",
    ".h",
    ".hh",
    ".hpp",
    ".glsl",
    ".frag",
    ".vert",
}
HASH_SUFFIXES = {".sh", ".bash", ".yml", ".yaml", ".toml", ".ini"}
CMAKE_SUFFIXES = {".cmake"}


def strip_cxx(text: str) -> str:
    """Remove C and C++ comments, leaving line structure intact.

    Literals are kept intact, including their line breaks, so embedded shaders
    and other multiline data count towards the budget as well.
    """
    out: list[str] = []
    index = 0
    end = len(text)
    while index < end:
        char = text[index]
        following = text[index + 1] if index + 1 < end else ""
        if char == "/" and following == "/":
            while index < end and text[index] != "\n":
                index += 1
        elif char == "/" and following == "*":
            index += 2
            while index + 1 < end and not (text[index] == "*" and text[index + 1] == "/"):
                if text[index] == "\n":
                    out.append("\n")
                index += 1
            index += 2
        elif char == '"' and index > 0 and text[index - 1] == "R":
            open_paren = text.find("(", index)
            delimiter = text[index + 1 : open_paren] if open_paren != -1 else ""
            closing = ")" + delimiter + '"'
            close_at = text.find(closing, open_paren) if open_paren != -1 else -1
            literal_end = close_at + len(closing) if close_at != -1 else end
            out.append(text[index:literal_end])
            index = literal_end
        elif char == '"' or (char == "'" and not (index > 0 and text[index - 1].isalnum())):
            literal_start = index
            index += 1
            while index < end and text[index] != char:
                index += 2 if text[index] == "\\" else 1
            index += 1
            out.append(text[literal_start:index])
        else:
            out.append(char)
            index += 1
    return "".join(out)


def strip_hash(text: str, *, bracket_comments: bool = False) -> str:
    """Remove ``#`` comments from CMake, shell and YAML.

    A ``#`` only opens a comment at the start of a word, which keeps ``$#`` and
    ``${#name}`` in shell intact. CMake bracket comments are handled when asked
    for.
    """
    out: list[str] = []
    index = 0
    end = len(text)
    quote = ""
    while index < end:
        char = text[index]
        if quote:
            out.append(char)
            if char == "\\" and index + 1 < end:
                out.append(text[index + 1])
                index += 2
                continue
            if char == quote:
                quote = ""
            index += 1
            continue
        if char in "\"'":
            quote = char
            out.append(char)
            index += 1
            continue
        if char == "#":
            starts_word = index == 0 or text[index - 1] in " \t\n;&|("
            if bracket_comments and text.startswith("#[[", index):
                close_at = text.find("]]", index)
                body = text[index:close_at] if close_at != -1 else text[index:]
                out.append("\n" * body.count("\n"))
                index = close_at + 2 if close_at != -1 else end
                continue
            if starts_word:
                while index < end and text[index] != "\n":
                    index += 1
                continue
        out.append(char)
        index += 1
    return "".join(out)


def strip_python(text: str) -> str:
    """Remove comments and actual docstrings, keeping multiline data strings."""
    lines = text.splitlines(keepends=True)
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type == tokenize.COMMENT:
            row, column = token.start
            end_column = token.end[1]
            lines[row - 1] = (
                lines[row - 1][:column] + " " * (end_column - column) + lines[row - 1][end_column:]
            )

    # AST columns are UTF-8 byte offsets, including when identifiers or
    # documentation contain non-ASCII characters.
    encoded_lines = [line.encode("utf-8") for line in lines]
    for node in ast.walk(ast.parse(text)):
        if not isinstance(node, (ast.Module, ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
            continue
        if ast.get_docstring(node, clean=False) is None:
            continue
        docstring = node.body[0]
        # ast declares the end of a node as optional because a synthesised tree
        # may leave it out. A parsed one never does, and the fallbacks say so.
        last_row = docstring.end_lineno or docstring.lineno
        last_column = docstring.end_col_offset or 0
        for row in range(docstring.lineno - 1, last_row):
            line = encoded_lines[row]
            start = docstring.col_offset if row == docstring.lineno - 1 else 0
            end = last_column if row == last_row - 1 else len(line.rstrip(b"\r\n"))
            encoded_lines[row] = line[:start] + b" " * (end - start) + line[end:]
    return b"".join(encoded_lines).decode("utf-8")


def code_lines(path: Path) -> int:
    """Count the lines of *path* that are neither blank nor comment.

    Unreadable files and unknown languages raise rather than quietly passing
    without being measured.
    """
    # A template such as buildinfo.cpp.in is measured as what it generates.
    name = path.name.removesuffix(".in")
    suffix = Path(name).suffix.lower()
    text = path.read_text(encoding="utf-8")

    if suffix in CXX_SUFFIXES:
        stripped = strip_cxx(text)
    elif suffix == ".py":
        stripped = strip_python(text)
    elif suffix in CMAKE_SUFFIXES or name == "CMakeLists.txt":
        stripped = strip_hash(text, bracket_comments=True)
    elif suffix in HASH_SUFFIXES:
        stripped = strip_hash(text)
    else:
        message = f"{path}: no line counting rule for this kind of file"
        raise ValueError(message)

    return sum(1 for line in stripped.splitlines() if line.strip())


def main() -> int:
    """Measure every path given and return 1 when one of them is over budget."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("paths", nargs="*", type=Path)
    parser.add_argument("--warn", type=int, default=WARN_DEFAULT)
    parser.add_argument("--fail", type=int, default=FAIL_DEFAULT)
    parser.add_argument("--report", action="store_true", help="print every file that was measured")
    arguments = parser.parse_args()

    status = 0
    for path in arguments.paths:
        try:
            count = code_lines(path)
        except (OSError, ValueError, SyntaxError, tokenize.TokenError) as error:
            print(f"error: cannot measure {path}: {error}", file=sys.stderr)
            status = 1
            continue
        if count > arguments.fail:
            print(
                f"error: {path}: {count} code lines, limit is {arguments.fail}",
                file=sys.stderr,
            )
            print(
                "       split it, or say in the commit message why it cannot be split",
                file=sys.stderr,
            )
            status = 1
        elif count > arguments.warn:
            print(
                f"warning: {path}: {count} code lines, budget runs out at {arguments.fail}",
                file=sys.stderr,
            )
        elif arguments.report:
            print(f"{path}: {count}")
    return status


if __name__ == "__main__":
    sys.exit(main())
