#!/usr/bin/env bash
# Prints the notes of one version from CHANGELOG.md (the text under the heading "## [<version>]" up to the next "## "); fails if there are none.
#   scripts/release-notes.sh 0.1
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
version="${1:?usage: release-notes.sh <version>}"
notes="$(awk -v v="$version" '
    /^## / { if (on) exit; if (index($0, "## [" v "]") == 1) { on = 1; next } }
    on { print }
' "$ROOT/CHANGELOG.md" | sed -e :a -e '/^[[:space:]]*$/{$d;N;ba' -e '}' | sed '/./,$!d')"
[ -n "$notes" ] || { echo "CHANGELOG.md has no notes for version $version (a heading '## [$version]' with text under it is needed)" >&2; exit 1; }
printf '%s\n' "$notes"
