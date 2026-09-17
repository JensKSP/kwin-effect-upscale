# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
"""What counts as an agent leftover, written down once.

Both checkers of AGENTS.md's first two rules read their patterns from here.
Before, each spelled them out for itself and the two had drifted: the commit
scan matched without regard to case and the file scan did not, so a file
carrying `Co-Authored-By: Claude` passed one check and failed the other. They
also disagreed on whether a bot address counts. One list cannot drift.

This module names every forbidden phrase, so a scan of it would report itself.
PATTERN_FILES lists the files that are exempt for that reason.
"""

from __future__ import annotations

import re

# Names that turn a co-authorship trailer into an attribution to a tool. The
# last one catches the noreply addresses that forges give their bots.
AGENT_NAMES = (
    "claude",
    "chatgpt",
    "gpt-[0-9]",
    "copilot",
    "gemini",
    "anthropic",
    "openai",
    "aider",
    "cursor",
    "bot@",
)

# Phrases that tell a reader how the code came about rather than what it does.
# Matched without regard to case: git writes `Co-authored-by:`, forges write
# `Co-Authored-By:`, and both are the same trailer.
ATTRIBUTION_PATTERNS = (
    r"Co-authored-by:[^\n]*(" + "|".join(AGENT_NAMES) + ")",
    r"Assisted-by:",
    r"SPDX-FileCopyrightText:.*(claude|chatgpt|copilot)",
    r"Generated with \[?(claude|chatgpt)",
    "\N{ROBOT FACE} Generated",
)

ATTRIBUTION = re.compile("|".join(ATTRIBUTION_PATTERNS), re.IGNORECASE)

# Paths that must never appear: the configuration, rules and history that an
# agent leaves in a checkout. AGENTS.md is deliberately not among them, because
# it is the one agent-facing file this project keeps.
FORBIDDEN_NAMES = (
    r"\.claude",
    r"\.claude-plugin",
    r"CLAUDE\.md",
    r"\.cursor",
    r"\.cursorrules",
    r"\.cursorignore",
    r"\.aider[^/]*",
    r"\.windsurf[^/]*",
    r"\.continue",
    r"\.codeium[^/]*",
    r"\.roo[^/]*",
    r"\.cline[^/]*",
    r"\.specstory",
    r"\.mcp\.json",
    r"copilot-instructions\.md",
    r"GEMINI\.md",
    "superpowers",
)

FORBIDDEN_PATHS = re.compile(
    r"(^|/)(" + "|".join(FORBIDDEN_NAMES) + r")(/|$)",
    re.IGNORECASE,
)

# Files that state the rule or the patterns, and would otherwise report
# themselves. Kept as a set of repository-relative paths.
PATTERN_FILES = frozenset(
    {
        "tools/ai_attribution.py",
        "tools/test_ai_attribution.py",
        "AGENTS.md",
    },
)


def is_attributed(text: str) -> bool:
    """Report whether *text* carries an attribution to a tool."""
    return ATTRIBUTION.search(text) is not None


def is_forbidden_path(path: str) -> bool:
    """Report whether *path* is agent tooling that must not be committed."""
    return FORBIDDEN_PATHS.search(path) is not None
