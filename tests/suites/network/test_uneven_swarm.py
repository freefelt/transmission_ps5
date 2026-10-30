import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, base64, subprocess, time, os, shutil
from harness import *
import harness; harness.AUTO_FOLDER = False
import swarm
PL = 1024 * 1024; SIZE = 192 * PL + 3 * 16384      # the size is not a multiple of a piece: the last piece is short (3 blocks), like in real torrents
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2; mkdir -p /mnt/usb2; mount -t tmpfs -o size=700m tmpfs /mnt/usb2")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    info, ih, path, n = swarm.make_torrent(bencode, "/tmp/swarm_seed", "UNEVEN", SIZE, PL)
    shared = []; seeders = [swarm.Seeder(path, PL, n, ih, rate_bps=10 * 1024 * 1024, have=h, log=shared) for h in ((0, n), (0, n * 3 // 4), (0, n // 2))]   # the peers have DIFFERENT pieces: the further the piece, the rarer it is
    tracker = swarm.Tracker([x.port for x in seeders])
    meta = base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tracker.port}/announce", "info": info})).decode()
    rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": "/mnt/usb2/torrents"}); t0 = time.time(); target = "/mnt/usb2/torrents/UNEVEN.part"; sizes = []; done = False; nxt = 0
    while time.time() - t0 < 80 and not done:
        try: sizes.append(os.stat(target).st_size)
        except FileNotFoundError: pass
        if time.time() >= nxt:
            ts = [t for t in rpc("torrent-get", fields=["name", "status", "percentDone"])["arguments"]["torrents"] if t["name"] == "UNEVEN"]; nxt = time.time() + 0.5
            done = bool(ts) and (ts[0]["status"] in (5, 6) or ts[0]["percentDone"] >= 1)
        time.sleep(0.002)
    order = list(dict.fromkeys(shared)); sizes = [s for i, s in enumerate(sizes) if i == 0 or s != sizes[i - 1]]; jumps = [b - a for a, b in zip([0] + sizes[:-1], sizes)]
    [x.stop() for x in seeders]; tracker.stop()
    check("it downloads completely from three peers with different pieces", done, order[:3])
    check("the first piece of the torrent is among the very first requested (three peers ask at the same moment, so the exact order of the first two may swap)", 0 in order[:3], order[:6])
    check("the SHORT LAST piece (it has fewer blocks, so Transmission used to treat it as 'closest to completion' and fetch it first) is not among the first 40 requested pieces", (n - 1) not in order[:40], (n - 1, order[:14]))
    check("no 'rarest piece first' jumping to the far end: the first 16 requested pieces all lie within the first 40 pieces", all(p < 40 for p in order[:16]), order[:16])
    check("the file grows gradually: the first size is small, there are many size steps and the biggest single jump is at most 40 MiB (before the fix it was 191 MiB at once)", sizes and sizes[0] <= 8 * PL and len(sizes) >= 60 and max(jumps) <= 40 * PL, (sizes[0] / PL if sizes else None, len(sizes), max(jumps) / PL if jumps else None))
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2 /tmp/swarm_seed")
sys.exit(summary())
