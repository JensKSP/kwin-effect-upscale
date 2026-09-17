#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Enforces the second rule of AGENTS.md and KDE's contribution rules: a commit
# may not name a tool as author or helper.
#
#   check-commit-trailers.sh [<range>]
#
# Without a range it checks the single commit at HEAD. CI passes the range of
# the branch under review. A pre-commit hook cannot do this check at all -- it
# never sees the message -- so CI is the place that actually guarantees it.

set -euo pipefail

range="${1:-HEAD~1..HEAD}"
if ! git rev-parse "$range" >/dev/null 2>&1; then
    range="HEAD"
fi

pattern='(Co-authored-by:[^\n]*(claude|chatgpt|gpt-[0-9]|copilot|gemini|anthropic|openai|aider|cursor|bot@)|Assisted-by:|Generated with \[?(Claude|ChatGPT)|🤖 Generated)'

status=0
while read -r sha; do
    [[ -n $sha ]] || continue
    if git log -1 --format='%B' "$sha" | grep -Piq "$pattern"; then
        echo "error: $(git log -1 --format='%h %s' "$sha") carries an AI attribution trailer" >&2
        status=1
    fi
done < <(git rev-list "$range" 2>/dev/null || git rev-list -1 HEAD)

if [[ $status -ne 0 ]]; then
    echo "KDE does not accept these trailers; see AGENTS.md and" >&2
    echo "https://community.kde.org/Guidelines_and_HOWTOs/Maintainers_and_Contributions" >&2
fi

exit $status
