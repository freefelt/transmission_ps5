#!/usr/bin/env bash
# Safety check before an ELF is handed out: the PS5 loader crashes on imports it does not know. This script compares
# the imports of the built ELFs with the allowed list scripts/allowed-imports.txt (the imports of the verified build 3.11
# plus what was added later and verified on a console: `sync`, since 0.0.57).
# Usage (from any folder):  scripts/check-imports.sh [ELF ...]
#   default: build/Transmission-<version>.elf and the embedded daemon .deps/build/parts/transmission_d.elf
# Exit code 0 = no extra imports. Override the nm tool with NM=... (llvm-nm is found automatically when it is installed).
set -uo pipefail
REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
ALLOWED="$REPO/scripts/allowed-imports.txt"
VERSION="$(tr -d '[:space:]' <"$REPO/VERSION")"
[ -f "$ALLOWED" ] || { echo "ERROR: $ALLOWED not found" >&2; exit 2; }
if [ -z "${NM:-}" ]; then
    for c in llvm-nm-18 llvm-nm "$(brew --prefix llvm@18 2>/dev/null)/bin/llvm-nm" nm; do
        if command -v "$c" >/dev/null 2>&1; then NM="$c"; break; fi
    done
fi
[ $# -gt 0 ] || set -- "$REPO/build/Transmission-$VERSION.elf" "$REPO/.deps/build/parts/transmission_d.elf"
allowed="$(sort -u "$ALLOWED")"
rc=0
for elf in "$@"; do
    [ -f "$elf" ] || { echo "ERROR: $elf not found" >&2; rc=2; continue; }
    extra="$("$NM" -D --undefined-only "$elf" 2>/dev/null | awk '{print $2}' | sort -u | comm -13 <(printf '%s\n' "$allowed") -)"
    if [ -z "$extra" ]; then echo "OK      $(basename "$elf"): no extra imports"
    else echo "ERROR   $(basename "$elf"): extra imports (the PS5 loader will crash on them):"; printf '          %s\n' $extra; rc=1; fi
done
exit "$rc"
