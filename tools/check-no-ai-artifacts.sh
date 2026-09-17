#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Enforces the first rule of AGENTS.md: this repository holds the plugin, and
# AGENTS.md is the only file in it that exists for an agent.
#
# Run without arguments it checks what is staged (this is how the ECM
# pre-commit hook calls it). With --all it checks every tracked file, which is
# what CI does, because a hook can be skipped with git commit --no-verify.

set -euo pipefail

mode="staged"
if [[ ${1:-} == "--all" ]]; then
    mode="all"
fi

if [[ $mode == "all" ]]; then
    mapfile -t files < <(git ls-files)
else
    mapfile -t files < <(git diff --cached --name-only --diff-filter=ACMR)
fi

if [[ ${#files[@]} -eq 0 ]]; then
    exit 0
fi

# Paths that must never appear. AGENTS.md is deliberately not among them.
forbidden_paths='(^|/)(\.claude|\.claude-plugin|CLAUDE\.md|\.cursor|\.cursorrules|\.cursorignore|\.aider[^/]*|\.windsurf[^/]*|\.continue|\.codeium[^/]*|\.roo[^/]*|\.cline[^/]*|\.specstory|\.mcp\.json|copilot-instructions\.md|GEMINI\.md|superpowers)(/|$)'

# Phrases that would tell a reader how the code came about rather than what it does.
forbidden_content='(SPDX-FileCopyrightText:.*(Claude|ChatGPT|Copilot)|Co-authored-by:[^\n]*(claude|chatgpt|gpt-[0-9]|copilot|gemini|anthropic|openai|aider|cursor)|Assisted-by:|Generated with \[?(Claude|ChatGPT)|🤖 Generated)'

status=0

for f in "${files[@]}"; do
    if [[ $f =~ $forbidden_paths ]]; then
        echo "error: $f is agent leftover; AGENTS.md is the only agent-facing file here" >&2
        status=1
    fi
done

for f in "${files[@]}"; do
    [[ -f $f ]] || continue
    # The two checkers spell out the patterns they look for, and AGENTS.md
    # states the rule; they would otherwise report themselves.
    [[ $f == "tools/check-no-ai-artifacts.sh" ]] && continue
    [[ $f == "tools/check-commit-trailers.sh" ]] && continue
    [[ $f == "AGENTS.md" ]] && continue
    if grep -PIlq "$forbidden_content" -- "$f" 2>/dev/null; then
        echo "error: $f mentions how it was written; say what the code does instead" >&2
        status=1
    fi
done

if [[ $status -ne 0 ]]; then
    echo "See AGENTS.md, section \"No AI artifacts in this repository\"." >&2
fi

exit $status
