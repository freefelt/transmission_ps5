# Contributing

Issues and pull requests are welcome. The short version is in the [README](README.md#-contributing); the full one is in [`CLAUDE.md`](CLAUDE.md), which is written for people and for coding agents alike.

- Read [`docs/payload-safety-notes.md`](docs/payload-safety-notes.md) before you touch files, the network, processes or devices: a payload has no sandbox, and the main aim of the project is to minimize the risk of harm to the console.
- Keep a change small and focused; say on which firmware and with which disks you tried it (the tests run on a Linux host, not on a console).
- `./build.sh` must pass (it checks the imports: the PS5 loader crashes on an import it does not know). The workflows **Build** and **Tests** run on every pull request.
- Changes to Transmission itself are patches ([`patches/README.md`](patches/README.md)); new text on the settings page goes into `locales/en.strings` and `locales/ru.strings`.
- Comments, commit messages and documentation are in English.
