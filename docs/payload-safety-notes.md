# Safety of PS5 payloads: discussion notes

Date: 2026-10-11 (translated from Russian). Context: the port of Transmission (`Transmission.elf`) and the wish to work out principles by
which payloads are written so that the risk of harming the console is as small as possible, and so that this is visible in the code.

Notation: **[verified]** from sources or tests, **[conclusion]** reasoning on verified facts, **[not verified]** general knowledge or a guess.

> Later changes that touch these notes: the "download folders" protection (section 5, "what is missing") exists since 0.0.31: new torrents
> can only go to the folders the user listed, and a folder on a disk that is not connected is never used. `scripts/check-imports.sh`
> compares the imports of the built ELF with `scripts/allowed-imports.txt` and fails the build on a new one (the PS5 loader crashes on an
> unknown import). The rest of the text is as it was written.

---

## 1. The main point: the threat models, and there is no real sandbox

- On the PS5 a payload can read and write kernel memory (in the SDK these are the `kernel_*` and `mdbg_*` functions). Through them it can
  give itself any rights it wants. **[verified: SDK headers]**
- So self-imposed limits protect against **mistakes** and against **abuse from outside** (for example through a network interface), but
  not against malicious code inside the payload itself. **[conclusion]**
- This is worth stating in the README of every payload, otherwise users will think the protection is stronger than it is.

## 2. What rights a payload really has

The result of the test payload `privcheck` on firmware 11.20 (started through Payload Manager / elfldr): **[verified]**

| Parameter | Value |
|---|---|
| `getuid / geteuid` | 1 / 0 (real uid 1, effective uid 0) |
| uid in the kernel (ucred) | 0, while `ruid` and `svuid` are 1 |
| Root of the process | the real root of the file system (chroot removed) |
| Jail | none (removed) |
| SCE caps | `00000000701c004000ff0000000000c0` (not the full set) |
| SCE authid | `0x480000001000000e` |
| `/dev/icc_fan` | can be opened for reading |

From the sources of elfldr: the loader raises the rights temporarily to load the ELF and then restores the caps and the authid. What
remains is effective uid 0 and a removed sandbox. **[verified]**

A correction to what was said earlier: the caps are **not** "all 0xff"; that was an inaccurate reading of the code.

A side conclusion: `access()` checks the real uid (1), so it may report "no write permission" where a process that runs with effective
uid 0 would in fact be able to write. To check permissions use `eaccess()` or a trial write. **[verified]**

## 3. What a foreign or faulty payload can do

**[conclusion]** Delete or damage files (saves, games, data), cause a kernel panic, change flags and permissions, read data and send it
over the network, register itself for autostart.

- A kernel panic is a software event and does not damage the hardware. The real harm: loss of data when a write is interrupted, an
  inconsistent file system on an external drive, and direct control of the hardware (the fan, the clocks). **[conclusion]**
- The recovery partition is not a folder in `/`: to ruin it one needs low-level access to the drive. Whether writing to system
  partitions is closed to a process with such rights is **unknown**. It cannot be claimed that this is impossible. **[not verified]**
- No reliable sources were found about irreversibility ("bricking") and PSN bans. A practical rule: work only in `/data`, on the external
  drive and in your own folders, and do not touch `/system*`, `/preinst`, `/dev`.

## 4. The network surface

**[verified in the sources]**

- The ELF loader listens on port 9021 on all interfaces without authorization: anyone on the local network can send code to the console.
- Payload Manager (port 8084) has no authorization in its code, and CORS is open to any site (`Access-Control-Allow-Origin: *`). What
  exactly can be done through its API was not checked.
- Consequence: security rests on trust in the local network. It is advisable to keep the console in a separate (guest) network.

## 5. Transmission: what is already done and what is missing

Already done **[verified, in the code and in the tests on the host]**:

- the calls `fork` and `execvp` are intercepted at link time and always fail (the scripts of Transmission cannot be started);
- access to the web interface is limited to private networks (`rpc-whitelist`); there is deliberately no password;
- the rights are not raised; writes to files are atomic; the versions and SHA-256 sums of the dependencies are pinned;
- the patches to third-party libraries are listed in the build script with their reasons (libdeflate, fmt);
- the first start chooses safe defaults (the download folder, notifications).

What is missing:

- **limits on paths**: through the web interface any download folder can be given, and the process runs with effective uid 0 and
  without a sandbox. Whoever is on the local network can write files anywhere (what could follow from that was not checked);
- an automatic check of the finished ELF for forbidden calls and extra paths.

## 6. Docker and isolation

- Docker will not work on the PS5: it needs a Linux kernel (namespaces, cgroups), while the PS5 has Sony's kernel based on FreeBSD, into
  which your own modules cannot be loaded. **[not verified on the console, general knowledge]**
- Linux does exist on the PS5 itself (for example ps5-linux-loader), but it is a replacement of the system and not a parallel environment.
- A container gives nothing to Transmission: it already runs natively. Ready-made Linux images of services are better run on a home server.
- There is no hard isolation on the PS5 (see section 1), so the aim is **minimal rights and visible self-imposed limits**, not "locking
  the process up".

## 7. Principles for writing payloads (a draft)

Three levels, and for each it is visible what it guarantees:

1. **A declaration.** A file `SAFETY.md` in the root of the repository: which paths the payload reads and writes, which ports it listens
   on, which devices it opens, which system functions it does not use (kernel memory, `fork`/`exec`, `ptrace`).
2. **A check at build time.** A script checks the finished ELF against this list and fails the build if something extra appears: new
   imports, calls of forbidden functions, new paths in strings. It is the **calls** that must be checked and not merely the presence of
   symbols, because the SDK pulls unused `kernel_*` functions into the ELF.
3. **Limits at run time.** Interception at link time (as already done for `fork`/`execvp`). For files: every open, write, rename and
   delete goes through one small file `sandbox.c` with a white list of paths; everything else is forbidden and written to the log. The
   limit of this level: calls from our code and from static libraries are intercepted, but not system calls made directly or inside
   Sony's libraries.

In practice: the dependencies and SHA-256 sums are pinned, the patches are listed with their reasons, writes are atomic, there are no
downloads and no auto-update on the fly, the defaults are safe, and the port and the rights of the services are limited.

## 8. Plan (in order, nothing started)

1. An audit and a "log only" mode for file calls: to see which paths Transmission really touches on the console.
2. A white list of paths based on that log.
3. Turning the ban on (with a log of violations).
4. An automatic check of the ELF at build time and a `SAFETY.md` for the repository.

In parallel (separate topics, already discussed): a settings server on its own port, an icon on the "Media" tab following the scheme of
Payload Manager, a sleep button for testing.

## 9. Open questions

- Whether file calls inside Sony's libraries are intercepted (it can only be checked with a test on the console).
- Which system partitions are writable for the process.
- Whether the rights of the process can be reduced (not root, fewer caps) with the means of the SDK without breaking the work.
- The cause of the panic when entering rest mode with Transmission running (it did not reproduce on the latest version).
