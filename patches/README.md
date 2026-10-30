# Patches

Everything in this folder changes or extends the **Transmission 4.1.3** sources. We never keep a modified copy of Transmission in the
repository: the build downloads the original archive (pinned by version and SHA-256 in `scripts/build-elf.sh`), unpacks it and applies
these patches, in order, on top of it. Our own code lives in `src/`; what is here is only what has to go *into* Transmission.

| Folder | Applied to | What it is |
|---|---|---|
| `transmission/` | the PS5 build **and** the test build | the behaviour changes we need (details below) |
| `ps5/` | the PS5 build only | small fixes without which Transmission does not build or run on the console |
| `web-client/` | the PS5 build and the test build | changes to the Transmission **web interface** (not a `.patch`, see below) |

("host" is the machine that builds and runs the tests, as opposed to the console. It is the same Transmission, built for Linux
instead of the PS5, so that the tests can start the real daemon.)

## The C++ patches

They are ordinary unified diffs (`patch -p1`), numbered; the number is the order. Each one starts with a short description.
The PS5 build applies `ps5/` then `transmission/`; the test build applies `transmission/` only (see
`scripts/build-elf.sh` and `tests/build/prepare-test-tree.sh`).

**`ps5/`**
- `0001-libdeflate-generic-c`: the console's clang defines `__SCE__`, so the AVX intrinsic headers libdeflate wants are missing; its x86 branch is turned off.
- `0002-fmt-plain-fwrite`: the console's libc has another `FILE` layout than the SDK headers, and fmt's fast path corrupts memory; plain `fwrite` is used.

**`transmission/`**
- `0001-rpc-server-listener-diagnostics`: the events of the listening socket are logged at warn level.
- `0002` to `0003`: **the download folders protection**. `torrent-add`, `torrent-set-location` and `session-set` only accept folders from the list kept by the manager; a torrent on a disk that is not connected is neither started nor verified. The logic itself is in the daemon wrapper (`src/daemon/guard.c`); the patches only add the calls.
- `0004`, `0006`, `0007`, `0008`: **downloading to FAT disks** (exFAT and FAT32 have no sparse files, a write far from the start fills the gap with zeroes and hangs the console): sequential download, strict piece order, a 64 MiB window, and a per-torrent choice of the "USB disk fix".
- `0005`: the torrent state is saved 20 s after changes instead of 6 minutes (a restart no longer returns a new torrent to the default folder), and the priority boost for the first and the last pieces of files is gone (it stretched files to their full size at once).
- `0009` to `0011`: the RPC method `ps5_folders` (the folder list with the state of the disks, and their file systems) and the `torrent-get` field `ps5_in_folder`, used by the web interface.

### Adding or changing a patch

1. Build once (`./build.sh`) so that the patched tree exists in `.deps/src/transmission-4.1.3`.
2. Copy the files you are going to change next to themselves, change them in `.deps/src/transmission-4.1.3`, and make the diff with the paths relative to the tree:
   ```sh
   cd .deps/src/transmission-4.1.3
   cp libtransmission/torrent.cc /tmp/torrent.cc.orig        # before editing
   # ... edit libtransmission/torrent.cc ...
   diff -u --label a/libtransmission/torrent.cc --label b/libtransmission/torrent.cc /tmp/torrent.cc.orig libtransmission/torrent.cc > /tmp/new.patch
   ```
3. Put the result in `patches/transmission/` as the next number with a short name, e.g. `0012-what-it-does.patch`, and write a few lines of description before the first `diff` line (`Subject: ...`).
4. Run `./build.sh` again. The tree is unpacked anew and all patches are applied from scratch whenever the set of patches changes, so a patch that does not apply on the pristine sources fails the build at once.
5. A patch in `transmission/` must not depend on the ones in `ps5/`: it is applied to both the PS5 and the test build.

Keep a patch small: put the logic in our own code (`src/daemon/`) and let the patch only call it. That keeps the move to a newer Transmission cheap.

### Moving to another Transmission version

The version is pinned (`TR_VER` and `TR_SHA256` in `scripts/build-elf.sh`, plus the file names in the test scripts). A move is a deliberate
step: change the pin, rebuild, fix the patches that no longer apply (and the patterns of `web-client/web_patch.pl`), then run the
full tests (`scripts/test.sh full`, `scripts/test.sh pages`) and try it on a console.

## `web-client/`: the web interface

The web interface of Transmission (the page on port 9091) is a minified JavaScript application. A normal diff of it would be one huge
line, so it is changed by a script instead:

- `web_patch.pl` copies the original `web/public_html`, adds our files, and makes substitutions in the original ones (text of the page,
  a few hooks in the minified code, translation calls). **Every substitution must find its place exactly once, otherwise the build
  fails**: that is how a move to another Transmission version announces itself.
- `js/`: `ps5-compat.js` (support for the console's old WebKit, folder tiles, the "USB disk fix" dialog, animations, the "Open folder"
  menu item), `ps5-panel.js` (the disk panel), `ps5-i18n.js` (translation).
- `css/`: the dark and light themes and the shared styles.
- `icons/`: the two icon sets (color and monochrome) as CSS; see `icons/README.md` for where the icons come from.

The result is generated into `.deps/build/generated/web_public_html` and built into the daemon. Texts of the web interface are
translated by the key "the English text itself" (see `locales/*.strings`); `node tools/web-i18n-check.js locales/ru.strings locales/en.strings .deps/build/generated/web_public_html` reports a text that is missing in the tables.
