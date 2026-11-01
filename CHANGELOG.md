# Changelog

All notable changes of the released versions. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the heading of every version is `## [<version>]` (no date: the date of a release is on its GitHub Releases page), and the text under it
becomes the notes of the GitHub release (see `docs/releasing.md`). A release only needs `VERSION` to be raised and the section to be written.

## [0.4]

### Changes
- The "Add Torrents" dialog works on iPad Safari again: the `.torrent` file filter (`accept`) is no longer set on touch devices, where the iOS Files picker showed `.torrent` files greyed out and they could not be selected. On a computer the `.torrent` filter is kept.

### Notes
- A web interface fix only; nothing else in the payload changed.

### Install
[How to install Transmission for PS5](https://github.com/freefelt/Transmission-PS5#-install)

## [0.3]

### Changes
- Transmission no longer runs while the console is in rest mode. The console does not freeze the payload in rest mode (it kept running, and the daemon used a whole CPU core for ten minutes), so the manager now watches the power state of the console (the system's event flags, the same way as ShadowMountPlus): when the console goes to sleep or shuts down, Transmission is stopped cleanly (the torrents are paused and remembered), and it starts again, and resumes them, when the console is awake. The Diagnostics card shows the power state.
- The notification says "Transmission stopped before sleep or shutdown" and that the torrents resume after wake-up or at the next start.
- The bell hears that the console's system service (game detection, keep awake) does not answer only if the silence lasts more than 2 minutes in total: right after a wake-up the console's services are slow, and that is only written to the log.

### Notes
- Developed and tested on firmware 11.20: going to sleep, waking up, a power off and a reboot were watched there. Other firmware versions are not verified.

### Install
[How to install Transmission for PS5](https://github.com/freefelt/Transmission-PS5#-install)

## [0.2]

### Changes
- Built with PS5 Payload SDK v0.45, the first line of SDKs that know firmware 13.60 (v0.41 does not know it, so 0.1 most probably did not start there); the diagnostics show the SDK version and the build time.
- Transmission 4.1.4 (fixes zero-byte files in torrents, a rare hang on shutdown and hardens `torrent-add`).
- The "system does not answer" message (game detection, keep awake) no longer stays forever after a console sleep: the 30 s count restarts at wake-up and the service comes back by itself, with a bell entry.
- Safer and clearer settings page: short captions, details in a "?" popup; download folders have square icon buttons (eject, open folder, remove), "drive not connected" is less alarming.
- Shorter names in the settings (“Pause during a game”: Do not pause / When the game is on screen / When the game is running; “Open folder” item: Do not show / BFpilot / PS5 Web File Manager); the rest mode method moved to the Advanced settings; the dialogs of the Transmission page show our icon instead of the stock logo.
- Starting and stopping the daemon no longer waits for a DNS server that does not answer (a network that lets only some DNS servers through used to cost 30 seconds each): name lookups of the daemon are given up after 4 seconds, two failures in a row stop further lookups for 30 seconds, and the DHT looks up its bootstrap name in the background.
- Diagnostics: a button “Manager log file” (shim.log from the disk, with the starts of other instances of the manager) and the same in the diagnostic snapshot.
- A new release is announced on the settings page: next to the version a link "update available N" to the latest release appears (your browser, not the console, asks GitHub at most once in a few hours; nothing is installed automatically).
- The build of the payload no longer reuses the Transmission build folder of another version (a warm `.deps` could silently build the old Transmission).
- An icon installed by an older version is recognised again ("Installed" instead of "not installed", and "Remove" now tells the system).

### Notes
- Developed and tested on firmware 11.20. Firmware 13.60 is covered by the SDK now, but this release was not tried there. Other firmware versions are not verified.

### Install
[How to install Transmission for PS5](https://github.com/freefelt/Transmission-PS5#-install)

## [0.1]

The first public release.

### Features
- Transmission 4.1.3 as one PS5 payload: the web interface on port 9091 (adapted for the PS5 browser: themes, icons, a disk panel, folder tiles) and a settings page on port 9092.
- Download folders protection: torrents can only be added to (and moved to) folders from a list; a folder on a disk that is not connected is never used.
- Care for USB disks: the per-torrent "USB disk fix" for exFAT and FAT32, warnings in the folder picker, safe eject of a USB drive through the system (for the console's extended storage the system does not offer it: eject it with the console itself).
- A watchdog that restarts Transmission when it hangs or after a network change; sleep or power off when all downloads finish; downloads pause while a game is on the screen.
- Notifications on the console and in a log (the bell on the settings page), English and Russian built in, other languages as plain text files.
- An optional icon on the "Media" tab.

### Notes
- Developed and tested on firmware 11.20. On 10.01 a user reported that it starts and downloads a torrent. Other firmware versions are not verified.

### Install
[How to install Transmission for PS5](https://github.com/freefelt/Transmission-PS5#-install)
