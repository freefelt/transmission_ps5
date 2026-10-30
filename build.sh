#!/usr/bin/env bash
# Builds the PS5 payload: build/Transmission-<version>.elf  (the version is in the VERSION file).
#
#   ./build.sh            # download the SDK and libraries (the first time), build, check the imports
#   ./build.sh --no-brew  # macOS: do not install missing tools with Homebrew
#
# Needs: bash, curl, tar, make, perl, cmake and clang 18 (macOS: Homebrew installs them; Linux: clang-18, lld-18,
# llvm-18-dev, cmake, make, perl, curl, xz-utils, unzip). Everything else is downloaded into .deps/.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
VERSION="$(tr -d '[:space:]' <"$ROOT/VERSION")"
"$ROOT/scripts/deps.sh" "$@"
"$ROOT/scripts/build-elf.sh" "$@"
"$ROOT/scripts/check-imports.sh"
printf '\nDone: %s\n' "$ROOT/build/Transmission-$VERSION.elf"
