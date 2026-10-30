#!/usr/bin/env bash
# Prepares a Linux machine for the tests (needs Linux, root and `mount -t tmpfs`; the PS5 build itself needs none of this).
# The tests write to /data, /mnt and /user: run them only in a VM or a container made for this.
#   sudo tests/setup-env.sh                  # creates /data /mnt /user/app, adds the address 192.0.2.2 to the loopback (swarm tests), installs the Node modules of the web tests (npm ci)
#   sudo tests/setup-env.sh --install-chrome # also installs Chromium for the browser tests (Playwright, into /opt/pw-browsers)
#   (or point CHROME_BIN to any Chrome/Chromium)
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
[ "$(uname -s)" = Linux ] || { echo "ERROR: the tests run only on Linux (use a VM)" >&2; exit 1; }
[ "$(id -u)" = 0 ] || { echo "ERROR: root is needed: sudo $0" >&2; exit 1; }
mkdir -p /data /mnt /user/app
# the swarm tests (network/) point Transmission to the peer 192.0.2.2: the machine must own that address (not persistent: run this script again after a reboot)
ip -4 addr show lo | grep -q ' 192.0.2.2/' || ip addr add 192.0.2.2/32 dev lo
command -v npm >/dev/null || { echo "ERROR: npm not found (Node 20 or newer is needed)" >&2; exit 1; }
if [ -n "${SUDO_USER:-}" ]; then sudo -u "$SUDO_USER" bash -c "cd '$ROOT/tests/web' && npm ci --no-audit --no-fund"; else (cd "$ROOT/tests/web" && npm ci --no-audit --no-fund); fi
if [ "${1:-}" = "--install-chrome" ]; then
    PLAYWRIGHT_BROWSERS_PATH=/opt/pw-browsers npx -y playwright@1.56.0 install --with-deps chromium
fi
echo "Done. Check the machine with: tests/check-env.sh"
