#!/usr/bin/env bash
# Checks the machine for the tests (Linux, root). Prints a table OK / WARNING / ERROR; exit code 1 on errors.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
bad=0
ok()   { printf '  %-44s OK      %s\n' "$1" "${2:-}"; }
warn() { printf '  %-44s WARNING %s\n' "$1" "${2:-}"; }
err()  { printf '  %-44s ERROR   %s\n' "$1" "${2:-}"; bad=1; }
have() { command -v "$1" >/dev/null 2>&1; }
echo "== system"
[ "$(uname -s)" = Linux ] && ok "Linux ($(uname -m))" || err "Linux" "the tests run only on Linux"
[ "$(uname -m)" = x86_64 ] && ok "architecture x86_64" || warn "architecture $(uname -m)" "verified only on x86_64"
[ "$(id -u)" = 0 ] && ok "root" || err "root" "root is needed (mount tmpfs, /data, /mnt, /user)"
t="$(mktemp -d)"; if mount -t tmpfs tmpfs "$t" 2>/dev/null; then umount "$t"; ok "mount -t tmpfs"; else err "mount -t tmpfs" "no right to mount (in Docker: --privileged)"; fi; rmdir "$t" 2>/dev/null
echo "== tools"
for c in clang-18 clang++-18 cmake make perl patch python3 node npm curl tar xz git objcopy nm; do have "$c" && ok "$c" "$($c --version 2>&1 | head -1 | cut -c1-60)" || err "$c" "not found"; done
have llvm-config-18 && ok "llvm-config-18" || warn "llvm-config-18" "needed only for the PS5 build on Linux (package llvm-18-dev)"
echo "== directories, Node modules and browser"
[ -d /data ] && [ -d /mnt ] && [ -d /user/app ] && ok "/data /mnt /user/app" || err "/data /mnt /user/app" "missing (tests/setup-env.sh creates them)"
[ -d "$ROOT/tests/web/node_modules/jsdom" ] && ok "jsdom" "$(node -p "require('$ROOT/tests/web/node_modules/jsdom/package.json').version" 2>/dev/null)" || err "jsdom" "missing (tests/setup-env.sh)"
[ -d "$ROOT/tests/web/node_modules/puppeteer-core" ] && ok "puppeteer-core" "$(node -p "require('$ROOT/tests/web/node_modules/puppeteer-core/package.json').version" 2>/dev/null)" || err "puppeteer-core" "missing (tests/setup-env.sh)"
CH="$(cd "$ROOT/tests/web" && node -e 'process.stdout.write(require("./chrome"))' 2>/dev/null)"
[ -n "$CH" ] && [ -x "$CH" ] && ok "Chromium" "$("$CH" --version 2>&1 | head -1)" || err "Chromium" "not found (tests/setup-env.sh --install-chrome, or set CHROME_BIN)"
ip -4 addr show lo 2>/dev/null | grep -q " 192.0.2.2/" && ok "address 192.0.2.2 on lo" || err "address 192.0.2.2 on lo" "missing: the swarm tests (network/) fail without it; tests/setup-env.sh adds it (again after every reboot)"
echo "== ports (must be free)"
for port in 9021 9091 9092 9093 9094; do python3 - "$port" <<'PY' && ok "port $port" || err "port $port" "in use: kill the old processes"
import socket,sys
s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
try: s.bind(("127.0.0.1",int(sys.argv[1])))
except OSError: sys.exit(1)
PY
done
echo "== builds"
[ -d "$ROOT/.deps/build/generated" ] && ok "generated files (after ./build.sh)" || warn ".deps/build/generated" "missing: run ./build.sh (the test build needs the web files)"
[ -d "$ROOT/.deps/test/transmission-4.1.4" ] && ok "Transmission for the tests" || warn ".deps/test/transmission-4.1.4" "missing: tests/build/build-test-binaries.sh"
[ -x "$ROOT/.deps/test/out/transmission_d_host" ] && [ -x "$ROOT/.deps/test/out/Transmission_mgr_host" ] && ok ".deps/test/out/*_host" || warn ".deps/test/out/*_host" "missing: tests/build/build-test-binaries.sh"
echo; [ "$bad" = 0 ] && echo "The machine is ready." || echo "There are errors: fix them and run the check again."
exit "$bad"
