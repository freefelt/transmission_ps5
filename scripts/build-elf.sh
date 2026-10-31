#!/usr/bin/env bash
# Step 2: libcurl + Transmission 4.1.4 + wrapper  ->  build/Transmission-<version>.elf
# Run after scripts/deps.sh (needs the SDK, zlib and OpenSSL from .deps). Normally you run ./build.sh in the repository root instead.
#
#   scripts/build-elf.sh            # build everything (idempotent)
#   scripts/build-elf.sh --no-brew  # do not call brew install (macOS)

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
SRCDIR="$REPO/src"
WEBCLIENT="$REPO/patches/web-client"
LOCALES="$REPO/locales"
VERSION="$(tr -d '[:space:]' <"$REPO/VERSION")"
# A test build handed out between releases: PS5TM_DEBUG=N ./build.sh. The page and shim.log then say "0.2 debug N" (two builds of one version can be told apart);
# the name of the file stays Transmission-<VERSION>.elf. Releases (CI) never set it.
SHIM_VER="$VERSION${PS5TM_DEBUG:+ debug $PS5TM_DEBUG}"
DEPS="$REPO/.deps"
DL="$DEPS/dl"
SRC="$DEPS/src"
LOGS="$DEPS/logs"
BUILD="$DEPS/build"
SDK_DIR="$DEPS/ps5-payload-sdk"
PARTS="$DEPS/build/parts"          # transmission_d.elf and transmission_i.elf: embedded into the final ELF
OUT="$REPO/build"                  # the final ELF: build/Transmission-<version>.elf
ELF="$OUT/Transmission-$VERSION.elf"

CURL_VER="8.11.1"
CURL_URL="https://github.com/curl/curl/releases/download/curl-8_11_1/curl-${CURL_VER}.tar.xz"
CURL_SHA256="c7ca7db48b0909743eaef34250da02c19bc61d4f1dcedd6603f109409536ab56"

TR_VER="4.1.4"
TR_URL="https://github.com/transmission/transmission/releases/download/${TR_VER}/transmission-${TR_VER}.tar.xz"
TR_SHA256="86e7aa020cdf64826447034bc85f4d3b7c848d8c0891d9dc9b9fddfb2c805d22"

CA_URL="https://raw.githubusercontent.com/certifi/python-certifi/2026.07.22/certifi/cacert.pem"
CA_SHA256="9cc2a774b5198dcff14d9be1e66091f538975d867ce029a96bce15a55dfd730f"

USE_BREW=1
[ "${1:-}" = "--no-brew" ] && USE_BREW=0

say() { printf '\n==> %s\n' "$*"; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
ncpu() { getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2; }

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

mkdir -p "$DL" "$SRC" "$LOGS" "$BUILD" "$OUT" "$PARTS"

# ---------- 1. Tools and environment ----------
say "Checking environment"
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
for t in curl tar make perl cmake; do have "$t" || die "not found: $t"; done
[ -f "$SDK_DIR/toolchain/prospero.sh" ] || die "SDK not found. Run scripts/deps.sh first"

# shellcheck disable=SC1091
source "$SDK_DIR/toolchain/prospero.sh"
SDK_VER="$(cat "$SDK_DIR/.ps5tm-sdk-version" 2>/dev/null || echo unknown)" # shown in the diagnostics
P="$PS5_SYSROOT$PREFIX"
for lib in libz.a libssl.a libcrypto.a; do
    [ -f "$P/lib/$lib" ] || die "missing $lib. Run scripts/deps.sh first"
done
JOBS="$(ncpu)"
# CMake 4.x rejects old third-party CMakeLists without this variable
export CMAKE_POLICY_VERSION_MINIMUM=3.5

# ---------- 2. libcurl ----------
# rebuilt when the pinned version changes (the stamp records which one is installed; a cache of an older run must not keep the old one)
CURL_ID="curl:$CURL_SHA256"
if [ "$(cat "$P/.ps5tm-curl" 2>/dev/null)" != "$CURL_ID" ]; then rm -f "$P/lib/libcurl.a" "$P/.ps5tm-curl"; fi
if [ ! -f "$P/lib/libcurl.a" ]; then
    say "libcurl $CURL_VER"
    fetch "$CURL_URL" "$DL/curl-$CURL_VER.tar.xz" "$CURL_SHA256"
    [ -d "$SRC/curl-$CURL_VER" ] || tar -xf "$DL/curl-$CURL_VER.tar.xz" -C "$SRC"
    (
        cd "$SRC/curl-$CURL_VER"
        make distclean >/dev/null 2>&1 || true
        export CPPFLAGS="-I$P/include" LDFLAGS="-L$P/lib" CFLAGS="-O2 -fPIC"
        ./configure --prefix="$PREFIX" --host=x86_64-pc-freebsd --enable-static --disable-shared \
            --with-openssl="$P" --with-zlib="$P" --without-brotli --without-zstd --without-libpsl \
            --without-libidn2 --without-libssh2 --without-nghttp2 --disable-ldap --disable-ldaps \
            --disable-docs --disable-manual --disable-ftp --disable-file --disable-dict \
            --disable-gopher --disable-imap --disable-mqtt --disable-pop3 --disable-rtsp \
            --disable-smb --disable-smtp --disable-telnet --disable-tftp
        make -j"$JOBS"
        make DESTDIR="$PS5_SYSROOT" install
    ) >"$LOGS/curl.log" 2>&1 || { tail -30 "$LOGS/curl.log"; die "curl build failed (log: $LOGS/curl.log)"; }
    printf '%s\n' "$CURL_ID" >"$P/.ps5tm-curl"
else
    say "libcurl: already built"
fi


# ---------- 3. Transmission ----------
# prospero.sh exports DESTDIR. The install of Transmission's third-party libraries
# (ExternalProject) picks it up and puts the files somewhere the compiler does not
# look later (dht/dht.h not found). curl above gets DESTDIR explicitly, it does not need this.
unset DESTDIR
TR_SRC="$SRC/transmission-$TR_VER"
TR_BUILD="$BUILD/transmission-$TR_VER-sdk-$SDK_VER" # one build folder per version of Transmission AND of the SDK: a folder configured for another version would silently build the old sources or with the old SDK
fetch "$TR_URL" "$DL/transmission-$TR_VER.tar.xz" "$TR_SHA256"

# Patched sources = the pristine archive + patches/ps5/*.patch + patches/transmission/*.patch (in this order).
# The tree is unpacked again whenever the patch set changes: a stamp next to it records which set it was built from.
PATCH_DIRS=("$REPO/patches/ps5" "$REPO/patches/transmission")
PATCH_STAMP="$SRC/.transmission-$TR_VER.patches"
sha256_stdin() { if have shasum; then shasum -a 256 | awk '{print $1}'; else sha256sum | awk '{print $1}'; fi; }
PATCH_ID="$( { printf '%s\n' "$TR_SHA256"; for d in "${PATCH_DIRS[@]}"; do for f in "$d"/*.patch; do cat "$f"; done; done; } | sha256_stdin )"
if [ ! -d "$TR_SRC" ] || [ "$(cat "$PATCH_STAMP" 2>/dev/null)" != "$PATCH_ID" ]; then
    say "Transmission $TR_VER: unpacking and applying patches"
    rm -rf "$TR_SRC" "$PATCH_STAMP"
    tar -xf "$DL/transmission-$TR_VER.tar.xz" -C "$SRC"
    for d in "${PATCH_DIRS[@]}"; do
        for p in "$d"/*.patch; do
            printf '  %s\n' "$(basename "$p")"
            ( cd "$TR_SRC" && patch -p1 -s -i "$p" ) || die "patch failed: $p"
        done
    done
    printf '%s\n' "$PATCH_ID" >"$PATCH_STAMP"
fi

# a build folder configured for another source tree (the repository was moved or renamed) is thrown away
if [ -f "$TR_BUILD/CMakeCache.txt" ] && ! grep -qx "CMAKE_HOME_DIRECTORY:INTERNAL=$TR_SRC" "$TR_BUILD/CMakeCache.txt"; then
    rm -rf "$TR_BUILD"
fi
if [ ! -f "$TR_BUILD/CMakeCache.txt" ]; then
    say "Configuring Transmission $TR_VER"
    mkdir -p "$TR_BUILD"
    "$CMAKE" -S "$TR_SRC" -B "$TR_BUILD" -G "Unix Makefiles" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$P" \
        -DOPENSSL_ROOT_DIR="$P" -DOPENSSL_USE_STATIC_LIBS=ON \
        -DOPENSSL_INCLUDE_DIR="$P/include" \
        -DOPENSSL_CRYPTO_LIBRARY="$P/lib/libcrypto.a" -DOPENSSL_SSL_LIBRARY="$P/lib/libssl.a" \
        -DCURL_INCLUDE_DIR="$P/include" -DCURL_LIBRARY="$P/lib/libcurl.a" \
        -DCMAKE_CXX_STANDARD_LIBRARIES="-L$P/lib -lz" \
        -DENABLE_DAEMON=ON -DENABLE_GTK=OFF -DENABLE_QT=OFF -DENABLE_MAC=OFF \
        -DRUN_CLANG_TIDY=OFF `# otherwise a clang-tidy found on the machine is run on the cross-compiled sources and fails` \
        -DENABLE_UTILS=OFF -DENABLE_CLI=OFF -DENABLE_TESTS=OFF -DENABLE_NLS=OFF \
        -DINSTALL_DOC=OFF -DINSTALL_WEB=ON -DREBUILD_WEB=OFF \
        -DWITH_INOTIFY=OFF -DWITH_KQUEUE=OFF -DWITH_SYSTEMD=OFF -DENABLE_UTP=ON \
        -DUSE_SYSTEM_EVENT2=OFF -DUSE_SYSTEM_DEFLATE=OFF -DUSE_SYSTEM_DHT=OFF \
        -DUSE_SYSTEM_MINIUPNPC=OFF -DUSE_SYSTEM_NATPMP=OFF -DUSE_SYSTEM_UTP=OFF \
        -DUSE_SYSTEM_B64=OFF -DUSE_SYSTEM_PSL=OFF \
        >"$LOGS/transmission-configure.log" 2>&1 \
        || { tail -30 "$LOGS/transmission-configure.log"; die "CMake configuration failed (log: $LOGS/transmission-configure.log)"; }
fi

say "Building Transmission (the slowest step, a few minutes)"
# Third-party libraries first: otherwise make -j may start compiling libtransmission
# before libdht has installed its headers (dht/dht.h not found).
( cd "$TR_BUILD" \
    && make -j"$JOBS" deflate dht event miniupnpc natpmp psl MadlerCrcany libb64 libutp wildmat \
    && make -j"$JOBS" transmission \
    && ( cd daemon && make daemon.cc.o daemon-posix.cc.o ) ) >"$LOGS/transmission-build.log" 2>&1 \
    || { grep -E ' error|undefined' "$LOGS/transmission-build.log" | head -20; die "Transmission build failed (log: $LOGS/transmission-build.log)"; }

# ---------- 4. Wrapper, web client and resources ----------
say "Wrapper, web client and resources"
GEN="$BUILD/generated"
mkdir -p "$GEN"
fetch "$CA_URL" "$DL/cacert.pem" "$CA_SHA256"

# bytes_header <C identifier> <file>: the file as a C array (identifier) plus its length (identifier_len).
bytes_header() {
    perl -e '
        my ($name, $path) = @ARGV;
        local $/; open(my $f, "<:raw", $path) or die "open: $!"; my $d = <$f>; close $f;
        my @b = unpack("C*", $d);
        print "static const unsigned char $name\[] = {\n";
        for (my $i = 0; $i < @b; $i += 16) {
            my $e = $i + 15 < $#b ? $i + 15 : $#b;
            print join(",", @b[$i..$e]), ",\n";
        }
        print "0};\nstatic const unsigned int ${name}_len = " . scalar(@b) . ";\n";
    ' "$1" "$2"
}
bytes_header cacert_pem "$DL/cacert.pem" >"$GEN/cacert.h"

# Transmission web interface for the PS5 browser: its WebKit does not support @media (prefers-color-scheme), so the
# upstream web client has no colors, no icons and no working "High contrast UI". We patch a copy (the original in $TR_SRC is left alone).
rm -rf "$GEN/web_assets" && mkdir -p "$GEN/web_assets"
cp "$REPO/resources/web/favicon.png" "$REPO/resources/web/apple-touch-icon.png" \
   "$WEBCLIENT/icons/ps5-icons-color.css" "$WEBCLIENT/icons/ps5-icons-mono.css" "$GEN/web_assets/"

perl "$WEBCLIENT/web_patch.pl" "$TR_SRC/web/public_html" "$GEN/web_public_html" "$GEN/web_assets" || die "failed to patch the web interface"
grep -q "ps5-theme-dark" "$GEN/web_public_html/index.html" 2>/dev/null || [ -f "$GEN/web_public_html/ps5-theme-dark.css" ] || die "no theme in the web interface"
grep -q "ps5: default theme for browsers without @media(prefers-color-scheme)" "$GEN/web_public_html/transmission-app.css" || die "no default theme in the web interface"

# Web interface files (without .map) -> byte arrays that the wrapper writes to disk
perl -MFile::Find -e '
    my $root = shift; my @files;
    find({ no_chdir => 1, wanted => sub {
        return unless -f $_ && $_ !~ /\.map$/;
        (my $rel = $_) =~ s{^\Q$root\E/}{};
        push @files, $rel;
    } }, $root);
    @files = sort @files;
    my $i = 0; my @table;
    print "/* generated by scripts/build-elf.sh */\n";
    for my $rel (@files) {
        open(my $f, "<:raw", "$root/$rel") or die "open $rel: $!";
        local $/; my $d = <$f>; close $f;
        my @b = unpack("C*", $d);
        print "static const unsigned char web_data_$i\[\] = {\n";
        for (my $k = 0; $k < @b; $k += 16) {
            my $e = $k + 15 < $#b ? $k + 15 : $#b;
            print join(",", @b[$k..$e]), ",\n";
        }
        print "0};\n";
        push @table, "    {\"$rel\", web_data_$i, " . scalar(@b) . "u},";
        $i++;
    }
    print "static const struct { const char *path; const unsigned char *data; unsigned int size; } web_files[] = {\n";
    print join("\n", @table), "\n};\n";
    print "static const unsigned int web_files_count = $i;\n";
' "$GEN/web_public_html" > "$GEN/webfiles.h"
[ -s "$GEN/webfiles.h" ] || die "failed to generate webfiles.h (no $GEN/web_public_html?)"

# The wrapper in C: src/common/*.c is compiled twice (once per role, the role is a macro), src/daemon/*.c only for the daemon
# (transmission_d.elf), the C files of src/manager/ only for the manager (Transmission.elf)
compile_c_role() { # role-macro  object-dir  sources...
    local macro="$1" odir="$2"; shift 2
    mkdir -p "$odir"; rm -f "$odir"/*.o
    for f in "$@"; do
        "$CC" -O2 -fPIC -Wall -Wextra -D"$macro" -DSHIM_VERSION="\"$SHIM_VER\"" -DPS5TM_SDK_VERSION="\"$SDK_VER\"" -I"$SRCDIR" -I"$SRCDIR/common" -I"$GEN" -c "$f" -o "$odir/$(basename "$f" .c).o"
    done
}
compile_c_role PS5TM_DAEMON "$GEN/obj-daemon" "$SRCDIR"/common/*.c "$SRCDIR"/daemon/*.c
compile_c_role PS5TM_MANAGER "$GEN/obj-manager" "$SRCDIR"/common/*.c "$SRCDIR"/manager/*.c

# Icons (PNG files in resources/) as C arrays for the ELF
bytes_header icon0_png "$REPO/resources/icon0.png" >"$GEN/icon0.h"
bytes_header icon128_png "$REPO/resources/icon128.png" >"$GEN/icon128.h"
bytes_header iconfav_png "$REPO/resources/iconfav.png" >"$GEN/iconfav.h"

# The settings page: src/manager/page/index.html with style.css and settings.js put in at the @@STYLE@@ and @@SCRIPT@@ lines,
# as one C string (the page has no external resources: it is served as a single document)
perl -e '
    my $dir = shift; local $/;
    sub slurp { open(my $f, "<:raw", "$dir/$_[0]") or die "open $_[0]: $!"; my $d = <$f>; close $f; return $d }
    my $html = slurp("index.html");
    for my $m (["\@\@STYLE\@\@\n", "style.css"], ["\@\@SCRIPT\@\@\n", "settings.js"]) {
        my $i = index($html, $m->[0]); die "no $m->[0] in index.html\n" if $i < 0;
        substr($html, $i, length($m->[0])) = slurp($m->[1]);
    }
    print "static char const kPageHtml[] = R\"HTML(" . $html . ")HTML\";\n";
' "$SRCDIR/manager/page" >"$GEN/ps5tm_page.h"

# Settings page languages: the built-in Russian and English tables (locales/*.strings of the project) become one header.
# Other languages are put by the user into /data/Transmission/locales, the built-in ones cannot be broken.
{ printf 'static char const kLangRuStrings[] = R"LANG(\n'; cat "$LOCALES/ru.strings"; printf '\n)LANG";\nstatic char const kLangEnStrings[] = R"LANG(\n'; cat "$LOCALES/en.strings"; printf '\n)LANG";\n'; } >"$GEN/ps5tm_lang_data.h"
# the manager (settings server, supervisor, notifications, system service ...): src/manager/*.cc, one object each
UI_OBJS=()
for f in "$SRCDIR"/manager/*.cc; do
    o="$GEN/ui_$(basename "$f" .cc).o"
    "$CXX" -O2 -fPIC -std=gnu++17 -Wall -Wextra -DPS5TM_TR_VERSION="\"$TR_VER\"" -I"$SRCDIR" -I"$GEN" -c "$f" -o "$o"
    UI_OBJS+=("$o")
done


# ---------- 5. Linking: the daemon first, then the manager with the embedded daemon ----------
# transmission_d.elf: the Transmission daemon with the PS5TM_DAEMON role wrapper. It is not started on its own: it is embedded
# in the manager, which puts it into /data/Transmission/ and starts it the same way Payload Manager does.
say "Linking build/transmission_d.elf (daemon)"
DAEMON_DIR="$TR_BUILD/daemon"
"$OBJCOPY" --redefine-sym main=tr_daemon_main \
    "$DAEMON_DIR/CMakeFiles/transmission-daemon.dir/daemon.cc.o" "$GEN/daemon_main.o"

BASE_LINK="$(cat "$DAEMON_DIR/CMakeFiles/transmission-daemon.dir/link.txt")"
BASE_LINK="${BASE_LINK//CMakeFiles\/transmission-daemon.dir\/daemon.cc.o/$GEN/daemon_main.o}"
DAEMON_WRAPS="-Wl,--wrap=getaddrinfo -Wl,--wrap=pthread_create -Wl,--wrap=fork -Wl,--wrap=execvp -Wl,--wrap=open -Wl,--wrap=close -Wl,--wrap=pwrite -Wl,--wrap=pread -Wl,--wrap=ftruncate -Wl,--wrap=rename -Wl,--wrap=mkdir -Wl,--wrap=unlink -Wl,--wrap=fsync -Wl,--wrap=stat -Wl,--wrap=lstat -Wl,--wrap=statvfs"
cmd="${BASE_LINK//-o transmission-daemon/-o $PARTS/transmission_d.elf $GEN/obj-daemon/*.o $DAEMON_WRAPS}"
( cd "$DAEMON_DIR" && eval "$cmd" ) >"$LOGS/link-transmission_d.log" 2>&1 \
    || { tail -30 "$LOGS/link-transmission_d.log"; die "linking transmission_d.elf failed (log: $LOGS/link-transmission_d.log)"; }
"$STRIP" --strip-debug "$PARTS/transmission_d.elf"
# the Transmission patch has to be inside: otherwise the build would silently produce an ELF without it (for example not rebuilt)
grep -aq "RPC restart requested" "$PARTS/transmission_d.elf" || die "transmission_d.elf has no rpc-server.cc patch"
# and it has to be built from the sources of THIS version (a stale build folder once produced 4.1.3 under the name of 4.1.4)
grep -aq "/transmission-$TR_VER/libtransmission/" "$PARTS/transmission_d.elf" || die "transmission_d.elf was not built from the Transmission $TR_VER sources (delete $BUILD/transmission-* and build again)"

# transmission_i.elf: the icon install helper. A separate short-lived process: all libSceAppInstUtil work happens in it
# (the libraries as in the working samples, BFpilot and websrv: SceIpmi first, then SceAppInstUtil),
# so a possible library hang does not affect the manager and Transmission. Embedded into the manager.
say "Building transmission_i.elf (icon install helper)"
"$CC" -Os -Wall -Wextra -o "$PARTS/transmission_i.elf" "$SRCDIR/icon-helper/ps5tm_icon_helper.c" -lSceIpmi -lSceAppInstUtil -lpthread >"$LOGS/link-transmission_i.log" 2>&1 \
    || { tail -30 "$LOGS/link-transmission_i.log"; die "building transmission_i.elf failed (log: $LOGS/link-transmission_i.log)"; }
"$STRIP" --strip-debug "$PARTS/transmission_i.elf"
# libSceIpmi must be in the list and must come BEFORE libSceAppInstUtil (which depends on it): otherwise the ELF hangs on load
for lib in libSceIpmi libSceAppInstUtil; do
    grep -aq "$lib" "$PARTS/transmission_i.elf" || die "transmission_i.elf has no dependency on $lib (without it the ELF hangs on load)"
done
[ "$(readelf -d "$PARTS/transmission_i.elf" | grep NEEDED | grep -n -E 'libSceIpmi|libSceAppInstUtil' | sed -n '1p' | grep -c libSceIpmi)" = "1" ] || die "in transmission_i.elf libSceIpmi does not come before libSceAppInstUtil"


# Transmission.elf: the manager (settings page, notifications, daemon supervision) with the daemon inside.
say "Embedding the daemon and linking build/Transmission.elf (manager)"
"$CC" -x assembler-with-cpp -DBLOB_PATH="\"$PARTS/transmission_d.elf\"" -c "$SRCDIR/asm/daemon_blob.S" -o "$GEN/daemon_blob.o"
"$CC" -x assembler-with-cpp -DICON_BLOB_PATH="\"$PARTS/transmission_i.elf\"" -c "$SRCDIR/asm/icon_blob.S" -o "$GEN/icon_blob.o"
"$CXX" -O2 -o "$ELF" "$GEN"/obj-manager/*.o "${UI_OBJS[@]}" "$GEN/daemon_blob.o" "$GEN/icon_blob.o" \
    -Wl,--wrap=pthread_create >"$LOGS/link-Transmission.log" 2>&1 \
    || { tail -30 "$LOGS/link-Transmission.log"; die "linking Transmission.elf failed (log: $LOGS/link-Transmission.log)"; }
"$STRIP" --strip-debug "$ELF"
[ "$(wc -c <"$ELF")" -gt "$(wc -c <"$PARTS/transmission_d.elf")" ] || die "the daemon was not embedded into Transmission.elf"

say "Done"
ls -l "$ELF"
printf 'SHA-256: %s\n' "$(sha256_of "$ELF")"
printf '%s\n' \
    "Send to the PS5 (ELF loader on 9021):  nc -q0 <IP_PS5> 9021 < build/Transmission-$VERSION.elf" \
    "Web interface:  http://<IP_PS5>:9091/   (the address arrives as a notification)" \
    "Settings page:  http://<IP_PS5>:9092/" \
    "Data and log:   /data/Transmission/  (config.cfg, shim.log, transmission.log, config/settings.json)"
