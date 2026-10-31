#!/usr/bin/env bash
# Step 1: ps5-payload-sdk + dependencies (zlib, libevent, OpenSSL) for the PS5.
# Everything lives inside the project folder (.deps), without sudo and without touching ~/.zshrc.
#
# Usage:
#   scripts/deps.sh            # SDK + all libraries
#   scripts/deps.sh --no-brew  # do not call brew install (macOS)

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
DEPS="$ROOT/.deps"
DL="$DEPS/dl"
SRC="$DEPS/src"
LOGS="$DEPS/logs"
SDK_DIR="$DEPS/ps5-payload-sdk"

SDK_VERSION="v0.45" # v0.42 is the first with firmware 13.60 (the startup code of the SDK knows the firmware versions); v0.41 stops at 13.40/13.42
SDK_URL="https://github.com/ps5-payload-dev/sdk/releases/download/${SDK_VERSION}/ps5-payload-sdk.zip"
SDK_SHA256="5fac64093d0e3e69f2584ce7935f24f703f622d9681f1495c85b9f00d1291a2c"

ZLIB_URL="https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
ZLIB_SHA256="9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
LIBEVENT_URL="https://github.com/libevent/libevent/releases/download/release-2.1.12-stable/libevent-2.1.12-stable.tar.gz"
LIBEVENT_SHA256="92e6de1be9ec176428fd2367677e61ceffc2ee1cb119035037a27d346b0403bb"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-3.0.15/openssl-3.0.15.tar.gz"
OPENSSL_SHA256="23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533"

USE_BREW=1
[ "${1:-}" = "--no-brew" ] && USE_BREW=0

say() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

sha256_of() {
    if have shasum; then shasum -a 256 "$1" | awk '{print $1}'
    else sha256sum "$1" | awk '{print $1}'; fi
}

fetch() { # url file sha256
    local url="$1" file="$2" want="$3"
    if [ ! -f "$file" ]; then
        printf 'Downloading %s\n' "$(basename "$file")"
        curl --fail --location --retry 3 --silent --show-error "$url" --output "$file"
    fi
    local got; got="$(sha256_of "$file")"
    if [ "$got" != "$want" ]; then
        rm -f "$file"
        die "wrong SHA-256 for $(basename "$file")
  expected: $want
  got:      $got"
    fi
}

ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2; }

mkdir -p "$DL" "$SRC" "$LOGS"

# ---------- 1. Host tools ----------
say "Checking tools"
case "$(uname -s)" in
    Darwin)
        if [ "$USE_BREW" -eq 1 ]; then
            have brew || die "Homebrew is required (https://brew.sh)"
            for f in llvm@18 cmake perl; do
                brew list --versions "$f" >/dev/null 2>&1 || brew install "$f"
            done
        fi
        LLVM_PREFIX="$(brew --prefix llvm@18 2>/dev/null || true)"
        [ -x "$LLVM_PREFIX/bin/llvm-config" ] || die "llvm@18 not found: brew install llvm@18"
        export LLVM_CONFIG="$LLVM_PREFIX/bin/llvm-config"
        ;;
    Linux)
        if have llvm-config-18; then export LLVM_CONFIG="$(command -v llvm-config-18)"
        elif have llvm-config; then export LLVM_CONFIG="$(command -v llvm-config)"
        else die "llvm-config is required (clang-18, lld-18, llvm-18-dev)"; fi
        ;;
    *) die "unsupported system: $(uname -s)" ;;
esac
for t in curl unzip tar make perl cmake; do have "$t" || die "not found: $t"; done
printf 'LLVM: %s (%s)\n' "$LLVM_CONFIG" "$("$LLVM_CONFIG" --version)"

# ---------- 2. SDK ----------
# the stamp records which version is installed: a folder .deps from an older version must not keep the old SDK (the libraries below live inside it and are built again)
if [ ! -f "$SDK_DIR/toolchain/prospero.sh" ] || [ "$(cat "$SDK_DIR/.ps5tm-sdk-version" 2>/dev/null)" != "$SDK_VERSION" ]; then
    say "ps5-payload-sdk $SDK_VERSION"
    fetch "$SDK_URL" "$DL/ps5-payload-sdk-$SDK_VERSION.zip" "$SDK_SHA256"
    rm -rf "$DEPS/.sdk-extract"; mkdir -p "$DEPS/.sdk-extract"
    unzip -q "$DL/ps5-payload-sdk-$SDK_VERSION.zip" -d "$DEPS/.sdk-extract"
    [ -f "$DEPS/.sdk-extract/ps5-payload-sdk/toolchain/prospero.sh" ] || die "the SDK archive has no toolchain"
    rm -rf "$SDK_DIR"
    mv "$DEPS/.sdk-extract/ps5-payload-sdk" "$SDK_DIR"
    printf '%s\n' "$SDK_VERSION" >"$SDK_DIR/.ps5tm-sdk-version"
    rmdir "$DEPS/.sdk-extract"
else
    say "SDK already installed: $SDK_DIR"
fi

# Cross-compilation environment: CC, AR, RANLIB, PS5_SYSROOT, PREFIX=/user/homebrew
# shellcheck disable=SC1091
source "$SDK_DIR/toolchain/prospero.sh"
PREFIX_DIR="$PS5_SYSROOT$PREFIX"          # where the libraries are really installed on the host
mkdir -p "$PREFIX_DIR/lib" "$PREFIX_DIR/include"
JOBS="$(ncpu)"

# The libraries are rebuilt when the pinned versions change: a stamp in the prefix records which set they were built from (a cache of an older run must not keep them)
LIBS_ID="zlib:$ZLIB_SHA256 libevent:$LIBEVENT_SHA256 openssl:$OPENSSL_SHA256"
if [ "$(cat "$PREFIX_DIR/.ps5tm-libs" 2>/dev/null)" != "$LIBS_ID" ]; then
    rm -f "$PREFIX_DIR"/lib/libz.* "$PREFIX_DIR"/lib/libevent* "$PREFIX_DIR"/lib/libssl.* "$PREFIX_DIR"/lib/libcrypto.* "$PREFIX_DIR"/lib64/libssl.* "$PREFIX_DIR"/lib64/libcrypto.* "$PREFIX_DIR/.ps5tm-libs"
fi

unpack() { # archive dirname
    [ -d "$SRC/$2" ] || tar -xf "$1" -C "$SRC"
}

# ---------- 3. zlib ----------
if [ ! -f "$PREFIX_DIR/lib/libz.a" ]; then
    say "zlib 1.3.1"
    fetch "$ZLIB_URL" "$DL/zlib-1.3.1.tar.gz" "$ZLIB_SHA256"
    unpack "$DL/zlib-1.3.1.tar.gz" zlib-1.3.1
    (
        cd "$SRC/zlib-1.3.1"
        make distclean >/dev/null 2>&1 || true
        # --uname=FreeBSD: otherwise configure on macOS replaces ar with Apple libtool
        CFLAGS="-O2 -fPIC" ./configure --prefix="$PREFIX" --static --uname=FreeBSD
        make -j"$JOBS"
        make DESTDIR="$PS5_SYSROOT" install
    ) >"$LOGS/zlib.log" 2>&1 || { tail -30 "$LOGS/zlib.log"; die "zlib build failed (log: $LOGS/zlib.log)"; }
else
    say "zlib: already built"
fi

# ---------- 4. libevent ----------
if [ ! -f "$PREFIX_DIR/lib/libevent.a" ]; then
    say "libevent 2.1.12"
    fetch "$LIBEVENT_URL" "$DL/libevent-2.1.12-stable.tar.gz" "$LIBEVENT_SHA256"
    unpack "$DL/libevent-2.1.12-stable.tar.gz" libevent-2.1.12-stable
    (
        cd "$SRC/libevent-2.1.12-stable"
        make distclean >/dev/null 2>&1 || true
        CFLAGS="-O2 -fPIC" ./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd \
            --enable-static --disable-shared --disable-openssl \
            --disable-samples --disable-libevent-regress --disable-debug-mode
        make -j"$JOBS"
        make DESTDIR="$PS5_SYSROOT" install
    ) >"$LOGS/libevent.log" 2>&1 || { tail -30 "$LOGS/libevent.log"; die "libevent build failed (log: $LOGS/libevent.log)"; }
else
    say "libevent: already built"
fi

# ---------- 5. OpenSSL ----------
if [ ! -f "$PREFIX_DIR/lib/libssl.a" ] && [ ! -f "$PREFIX_DIR/lib64/libssl.a" ]; then
    say "OpenSSL 3.0.15 (this is the slowest step)"
    fetch "$OPENSSL_URL" "$DL/openssl-3.0.15.tar.gz" "$OPENSSL_SHA256"
    unpack "$DL/openssl-3.0.15.tar.gz" openssl-3.0.15
    (
        cd "$SRC/openssl-3.0.15"
        make distclean >/dev/null 2>&1 || true
        ./Configure BSD-x86_64 --prefix="$PREFIX" --openssldir="$PREFIX/ssl" \
            no-shared no-tests no-asm no-dso no-engine no-ui-console -fPIC
        make -j"$JOBS" build_libs
        make DESTDIR="$PS5_SYSROOT" install_dev
    ) >"$LOGS/openssl.log" 2>&1 || { tail -30 "$LOGS/openssl.log"; die "OpenSSL build failed (log: $LOGS/openssl.log)"; }
else
    say "OpenSSL: already built"
fi

printf '%s\n' "$LIBS_ID" >"$PREFIX_DIR/.ps5tm-libs"

# ---------- 6. Summary ----------
say "Done. Libraries installed in $PREFIX_DIR"
ls -l "$PREFIX_DIR"/lib/*.a "$PREFIX_DIR"/lib64/*.a 2>/dev/null || true
printf '\n%s\n' "Next: scripts/build-elf.sh (or just ./build.sh)"
