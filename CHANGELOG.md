# Changelog

All notable changes of the released versions. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the heading of
every version is `## [<version>]`, and the text under it becomes the notes of the GitHub release (see `docs/releasing.md`).

## [0.1] - 2026-10-30

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
