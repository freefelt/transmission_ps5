import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, base64, subprocess, time, os, shutil
from harness import *
import harness; harness.AUTO_FOLDER = False
import swarm
PL = 1024 * 1024
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["id", "name", "status", "percentDone"])["arguments"]["torrents"] if t["name"] == name]; return ts[0] if ts else None
def download(name, info, ih, path, n, folder, watch):
    """Downloads a torrent from a local seeder; returns the order of the requested pieces and the list of .part file sizes (watch) as the download goes."""
    seeder = swarm.Seeder(path, PL, n, ih, rate_bps=24 * 1024 * 1024); tracker = swarm.Tracker([seeder.port])
    meta = base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tracker.port}/announce", "info": info})).decode()
    rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": folder}); t0 = time.time(); sizes = {w: [] for w in watch}; next_poll = 0; done = False
    while time.time() - t0 < 80 and not done:
        for w in watch:
            try: sizes[w].append(os.stat(w + ".part").st_size)
            except FileNotFoundError: sizes[w].append(None)
        if time.time() >= next_poll:
            t = tget(name); next_poll = time.time() + 0.5
            done = bool(t) and (t["status"] in (5, 6) or t["percentDone"] >= 1)
        time.sleep(0.002)
    order = list(dict.fromkeys(seeder.requests)); seq = rpc("torrent-get", fields=["name", "sequentialDownload", "sequential_download"])["arguments"]["torrents"]
    rpc("torrent-remove", ids=[t["id"] for t in rpc("torrent-get", fields=["id"])["arguments"]["torrents"]], **{"delete-local-data": True}); seeder.stop(); tracker.stop(); time.sleep(1)
    return order, sizes, done, seq
sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2; mkdir -p /mnt/usb2; mount -t tmpfs -o size=700m tmpfs /mnt/usb2")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    print("== a torrent of ONE file, folder on a USB disk")
    info, ih, path, n = swarm.make_torrent(bencode, "/tmp/swarm_seed", "ONE", 96 * PL, PL)
    order, sizes, done, seq = download("ONE", info, ih, path, n, "/mnt/usb2/torrents", ["/mnt/usb2/torrents/ONE"])
    s = [x for x in sizes["/mnt/usb2/torrents/ONE"] if x is not None]; jumps = [b - a for a, b in zip([0] + s[:-1], s)]
    check("it downloads completely over the network", done, order[:3])
    check("the pieces are requested STRICTLY IN ORDER from the start: 0, 1, 2, ... (no 'last piece first')", order[:16] == list(range(16)), order[:16])
    check("the file grows gradually: the biggest single step is at most 2 MiB (no jump to the full size, which is a minutes-long zero fill on exFAT)", len(s) > 40 and max(jumps) <= 2 * PL, (len(s), max(jumps) / PL if jumps else None))
    print("== a torrent of THREE files (the end of every file used to be requested at once)")
    info, ih, path, n = swarm.make_multi(bencode, "/tmp/swarm_seed", "MULTI", [32 * PL, 32 * PL, 32 * PL], PL)
    base = "/mnt/usb2/torrents/MULTI/"
    order, sizes, done, seq = download("MULTI", info, ih, path, n, "/mnt/usb2/torrents", [base + "f0.bin", base + "f1.bin", base + "f2.bin"])
    s0 = [x for x in sizes[base + "f0.bin"] if x is not None]; s1 = [x for x in sizes[base + "f1.bin"] if x is not None]; s2 = [x for x in sizes[base + "f2.bin"] if x is not None]
    j0 = [b - a for a, b in zip([0] + s0[:-1], s0)]; j1 = [b - a for a, b in zip([0] + s1[:-1], s1)]; j2 = [b - a for a, b in zip([0] + s2[:-1], s2)]
    check("three files: it downloads completely", done, order[:3])
    check("three files: the pieces are requested in order from the start (not the end of each file first)", order[:16] == list(range(16)), order[:16])
    check("three files: no file jumps to its full size (the biggest step of every file is at most 2 MiB)", all(len(x) > 10 and max(y) <= 2 * PL for x, y in ((s0, j0), (s1, j1), (s2, j2))), [(len(x), max(y) / PL) for x, y in ((s0, j0), (s1, j1), (s2, j2))])
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2 /tmp/swarm_seed")
sys.exit(summary())
