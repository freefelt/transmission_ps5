#!/usr/bin/env bash
# Prepares the Transmission source tree for the TEST build: unpacks the 4.1.3 archive and applies
# patches/transmission/*.patch (the same patches the PS5 build uses).
# The tree is unpacked again whenever the patch set changes.
#   tests/build/prepare-test-tree.sh                      # tree in .deps/test/transmission-4.1.3
#   TR_SRC=/other/dir/transmission-4.1.3 tests/build/prepare-test-tree.sh
# Needs the archive .deps/dl/transmission-4.1.3.tar.xz (downloaded by ./build.sh) or TR_TARBALL=/path/to/archive.
set -euo pipefail
. "$(dirname "${BASH_SOURCE[0]}")/env.sh"
TARBALL="${TR_TARBALL:-$REPO/.deps/dl/transmission-4.1.3.tar.xz}"
PATCH_DIRS=("$REPO/patches/transmission")
STAMP="$TR_SRC.patches"
sha256_stdin() { if command -v shasum >/dev/null 2>&1; then shasum -a 256 | awk '{print $1}'; else sha256sum | awk '{print $1}'; fi; }
PATCH_ID="$( for d in "${PATCH_DIRS[@]}"; do for f in "$d"/*.patch; do cat "$f"; done; done | sha256_stdin )"
if [ ! -d "$TR_SRC" ] || [ "$(cat "$STAMP" 2>/dev/null)" != "$PATCH_ID" ]; then
    [ -f "$TARBALL" ] || { echo "ERROR: no archive $TARBALL (run ./build.sh first or set TR_TARBALL)" >&2; exit 1; }
    rm -rf "$TR_SRC" "$STAMP"
    parent="$(dirname "$TR_SRC")"; mkdir -p "$parent"
    tmp="$(mktemp -d "$parent/.extract.XXXXXX")"; tar -xf "$TARBALL" -C "$tmp"
    mv "$tmp/transmission-4.1.3" "$TR_SRC"; rmdir "$tmp"
    echo "[test build] unpacked into $TR_SRC"
    for d in "${PATCH_DIRS[@]}"; do
        for p in "$d"/*.patch; do
            ( cd "$TR_SRC" && patch -p1 -s -i "$p" ) || { echo "ERROR: patch failed: $p" >&2; exit 1; }
        done
    done
    printf '%s\n' "$PATCH_ID" >"$STAMP"
    # The build directory still has objects from the previous patch set, and the files from the archive carry old
    # timestamps, so make would not rebuild them. Force a clean build whenever the patch set changes.
    rm -rf "$TR_BUILD"
fi
echo "[test build] tree $TR_SRC is ready (all patches applied)"
