# Third-party notices

Transmission for PS5 is licensed under the **GPL-3.0-or-later** (see [`LICENSE`](LICENSE)). It is built from, and linked with, the
components below, each of which keeps its own license. The authoritative texts are in the sources of the components (the build
downloads them into `.deps/`); this file is a summary and not legal advice.

## Linked into `Transmission-<version>.elf`

| Component | Version | License |
|---|---|---|
| [Transmission](https://transmissionbt.com) | 4.1.4 | GPL-2.0 or GPL-3.0 (your choice) or any later version endorsed by Mnemosyne LLC; linking with OpenSSL is explicitly allowed. Some of its files have more permissive licenses. The patches in `patches/transmission/` and `patches/ps5/` are derived from it and follow its terms. |
| [libevent](https://libevent.org) | 2.1.12 | BSD-3-Clause |
| [zlib](https://zlib.net) | 1.3.1 | zlib license |
| [OpenSSL](https://www.openssl.org) | 3.0.15 | Apache-2.0 |
| [curl](https://curl.se) | 8.11.1 | curl license (MIT-style) |
| Mozilla CA certificates (through [certifi](https://github.com/certifi/python-certifi)) | 2026.07.22 | MPL-2.0 |
| PS5 payload SDK ([ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk), John Törnblom) | v0.45 | GPL-3.0-or-later for its scripts, headers and start files (as stated in their headers); it also carries LLVM's libc++ (Apache-2.0 with LLVM exception) and FreeBSD-derived headers (BSD) |

Libraries bundled in the Transmission 4.1.4 sources (`third-party/`), built into the daemon:

| Library | License |
|---|---|
| libdeflate | MIT |
| {fmt} | MIT |
| libpsl | MIT |
| libutp | MIT |
| dht | MIT |
| miniupnpc, libnatpmp | BSD-3-Clause |
| crcany (Mark Adler) | zlib license |
| libb64 | public domain |
| wildmat | public domain (no license file) |
| small, utfcpp, wide-integer | Boost Software License 1.0 |
| fast_float | Apache-2.0 (upstream also offers MIT and BSL-1.0) |
| RapidJSON | MIT |

(googletest is in the same folder but is not built.)

## The web interface and the settings page

The Transmission web interface that the daemon serves is Transmission's own (GPL, as above); this project patches it
(`patches/web-client/`). The icons used on the pages:

| Icons | Source | License |
|---|---|---|
| monochrome set | [Lucide](https://github.com/lucide-icons/lucide) | ISC |
| color set: toolbar buttons, USB drive | [MingCute](https://github.com/Richard9394/MingCute) | Apache-2.0 |
| color set: torrent type icons | [Fluent Emoji](https://github.com/microsoft/fluentui-emoji) | MIT |
| color set: internal drive | [Fluent UI System Icons](https://github.com/microsoft/fluentui-system-icons) | MIT |
| M.2 SSD icon | drawn for this project | GPL-3.0-or-later |

The icon sets were taken through [Iconify](https://iconify.design).

The icon of the application (the Transmission icon on the "Media" tab and the favicons) is the original Transmission icon
by [Zabriskije](https://github.com/Zabriskije) ([discussion](https://github.com/transmission/transmission/discussions/5479)),
used with attribution; the terms of that artwork are its author's.

## Acknowledgements

Projects whose examples and sources helped the development (credited as on the settings page):
[BFpilot](https://github.com/ItsBlurf/BFpilot) (icon registration through SceIpmi and SceAppInstUtil), Payload Manager (process names,
icon installation, process memory), ShadowMountPlus, websrv and ftpsrv (installing a shortcut app),
[ps5-linux-loader](https://github.com/ps5-linux/ps5-linux-loader) (the way to enter rest mode),
[ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) (the way to ask the system to eject a USB drive over IPMI, and the way to read the power state of the console from the event flags `SceSystemStateMgrInfo` and `SceSystemStateMgrStatus`), PS5-Power-Payloads-Project (power off and rest mode calls), [ps5-library](https://github.com/deox1111/ps5-library) (ideas about
working in rest mode and device pairing) and PS5-Suite (a note about the official power off calls).
