import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import sys, base64, subprocess, time, os, shutil, random
from harness import *
import harness; harness.AUTO_FOLDER = False
import swarm
MiB = 1024 * 1024
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def tinfo(name):
    ts = [t for t in rpc("torrent-get", fields=["name", "status", "percentDone", "sequentialDownload", "sequential_download"])["arguments"]["torrents"] if t["name"] == name]; return ts[0] if ts else None
sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2; mkdir -p /mnt/usb2; mount -t tmpfs -o size=900m tmpfs /mnt/usb2")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    # A: one large file, uneven seeding
    infoA, ihA, pathA, nA = swarm.make_torrent(bencode, "/tmp/swarm_seed", "BIG", 400 * MiB + 3 * 16384, 4 * MiB)
    logA = []; seedsA = [swarm.Seeder(pathA, 4 * MiB, nA, ihA, rate_bps=8 * MiB, have=h, log=logA) for h in ((0, nA), (0, nA * 3 // 4), (0, nA // 2))]; trA = swarm.Tracker([x.port for x in seedsA])
    # B: many small files
    random.seed(7); sizesB = [random.randint(20, 400) * 1024 for _ in range(1500)]
    infoB, ihB, pathB, nB = swarm.make_multi(bencode, "/tmp/swarm_seed", "MANY", sizesB, 1 * MiB)
    logB = []; seedsB = [swarm.Seeder(pathB, 1 * MiB, nB, ihB, rate_bps=8 * MiB, have=h, log=logB) for h in ((0, nB), (0, nB * 3 // 4))]; trB = swarm.Tracker([x.port for x in seedsB])
    mk = lambda info, tr: base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tr.port}/announce", "info": info})).decode()
    rpc("torrent-add", metainfo=mk(infoA, trA), paused=False, **{"download-dir": "/mnt/usb2/torrents"}); t0 = time.time()
    tgtA = "/mnt/usb2/torrents/BIG.part"; trace = []; addedB = None; last = None; doneA = doneB = False; nxt = 0
    while time.time() - t0 < 120 and not (doneA and doneB):
        try: st = os.stat(tgtA).st_size
        except FileNotFoundError: st = None
        if st != last and st is not None: trace.append((round(time.time() - t0, 2), st, 1 if addedB else 0)); last = st
        pctA = None
        if time.time() >= nxt:
            a = tinfo("BIG"); b = tinfo("MANY"); nxt = time.time() + 0.5
            pctA = a["percentDone"] if a else 0; doneA = bool(a) and (a["status"] in (5, 6) or a["percentDone"] >= 1); doneB = bool(b) and (b["status"] in (5, 6) or b["percentDone"] >= 1)
            if addedB is None and pctA >= 0.25:
                rpc("torrent-add", metainfo=mk(infoB, trB), paused=False, **{"download-dir": "/mnt/usb2/torrents"}); addedB = time.time() - t0; print(f"[{addedB:.1f}s] torrent B (1500 small files) added while A is at {pctA * 100:.0f}%")
        time.sleep(0.002)
    sizes = [x[1] for x in trace]; jumps = [(trace[i][1] - (trace[i - 1][1] if i else 0), trace[i][2]) for i in range(len(trace))]
    jb = [j for j, f in jumps if f]; ja = [j for j, f in jumps if not f]
    print("A finished:", doneA, "| B finished:", doneB, "| total %.1f s" % (time.time() - t0))
    print("A: size steps BEFORE B was added: %d, biggest %.1f MiB | AFTER B was added: %d, biggest %.1f MiB" % (len(ja), (max(ja) if ja else 0) / MiB, len(jb), (max(jb) if jb else 0) / MiB))
    print("A: first requested pieces (distinct):", list(dict.fromkeys(logA))[:12], "| A pieces total", nA)
    after = [p for p in dict.fromkeys(logA)][len(dict.fromkeys(logA)) // 2:][:12]
    print("A: sequentialDownload:", tinfo("BIG").get("sequentialDownload", tinfo("BIG").get("sequential_download")) if tinfo("BIG") else None, "| B:", tinfo("MANY").get("sequentialDownload", tinfo("MANY").get("sequential_download")) if tinfo("MANY") else None)
    nfiles = sum(len(f) for _, _, f in os.walk("/mnt/usb2/torrents/MANY")) if os.path.isdir("/mnt/usb2/torrents/MANY") else 0
    print("B: files on disk:", nfiles)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2 /tmp/swarm_seed")
