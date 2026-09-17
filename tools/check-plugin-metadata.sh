#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jens Koehler <kwin-effect-upscale@koehler-speyer.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Validates the effect's KPluginMetaData file against the schema KDE Frameworks
# ships, which is what ECM's JSON_SCHEMA commit hook does. It is not a
# pre-commit hook here because the schema comes from libkf6coreaddons-data and
# the linting job does not install KDE Frameworks; it runs in the container and
# in the CI jobs that build, where the schema is present by definition.
#
# A wrong key or a misspelled category in that file makes KWin drop the effect
# without saying why, so this is worth a check of its own.

set -euo pipefail

schema=${KPLUGINMETADATA_SCHEMA:-/usr/share/kf6/jsonschema/kpluginmetadata.schema.json}

if ! command -v check-jsonschema >/dev/null; then
    echo "error: check-jsonschema is not installed" >&2
    echo "       pipx install check-jsonschema, or run this in containers/trixie" >&2
    exit 1
fi

if [[ ! -f $schema ]]; then
    echo "error: no KPluginMetaData schema at $schema" >&2
    echo "       install libkf6coreaddons-data, or set KPLUGINMETADATA_SCHEMA" >&2
    exit 1
fi

# Only files that actually are plugin metadata: a JSON file with a KPlugin
# object at its root. That is the same rule ECM's hook applies.
mapfile -t candidates < <(git ls-files '*.json')

metadata=()
for file in "${candidates[@]}"; do
    if python3 -c "
import json, sys
with open(sys.argv[1], encoding='utf-8') as handle:
    document = json.load(handle)
sys.exit(0 if isinstance(document, dict) and 'KPlugin' in document else 1)
" "$file" 2>/dev/null; then
        metadata+=("$file")
    fi
done

if [[ ${#metadata[@]} -eq 0 ]]; then
    echo "error: no KPluginMetaData file found; this project ships one" >&2
    exit 1
fi

echo "Validating against $schema:"
printf '  %s\n' "${metadata[@]}"
check-jsonschema --schemafile "$schema" "${metadata[@]}"
