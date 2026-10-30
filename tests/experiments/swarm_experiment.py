import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import sys, json, base64, subprocess, time, os, shutil
from harness import *
import harness; harness.AUTO_FOLDER = False
import swarm
CASES = sys.argv[1:] or ["usb", "internal"]      # usb = /mnt/usb2/torrents (sequential mode), internal = /data/g31 (normal order)
SIZE = 192 * 1024 * 1024 + 3 * 16384; PL = 1024 * 1024        # size is NOT a multiple of the piece: the last piece is short (3 blocks), as in real torrents
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2; mkdir -p /mnt/usb2; mount -t tmpfs -o size=700m tmpfs /mnt/usb2")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True}); api("POST", "/api/folders", {"action": "add", "path": "/data/g31", "confirm": True})
    for case in CASES:
        folder = "/mnt/usb2/torrents" if case == "usb" else "/data/g31"; name = "SWARM_" + case; target = f"{folder}/{name}"
        info, ih, path, n = swarm.make_torrent(bencode, "/tmp/swarm_seed", name, SIZE, PL)
        # three seeders with DIFFERENT pieces (as in a real swarm): the first has all pieces, the second the first 3/4, the third the first half. The farther the piece, the rarer it is.
        shared = []; seeders = [swarm.Seeder(path, PL, n, ih, rate_bps=10 * 1024 * 1024, have=h, log=shared) for h in ((0, n), (0, n * 3 // 4), (0, n // 2))]
        tracker = swarm.Tracker([x.port for x in seeders]); seeder = seeders[0]
        meta = base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tracker.port}/announce", "info": info})).decode()
        r = rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": folder}); t0 = time.time(); samples = []; last = object(); status = None; next_poll = 0
        while time.time() - t0 < 80:
            try: sx = os.stat(target + ".part"); st = sx.st_size; blk = sx.st_blocks * 512
            except FileNotFoundError: st = None; blk = None
            if st != last: samples.append((round(time.time() - t0, 3), st, blk)); last = st
            if time.time() >= next_poll:
                ts = [t for t in rpc("torrent-get", fields=["name", "status", "percentDone", "sequentialDownload", "sequential_download"])["arguments"]["torrents"] if t["name"] == name]
                status = ts[0]["status"] if ts else None; seq = ts[0].get("sequentialDownload", ts[0].get("sequential_download")) if ts else None; pct = ts[0]["percentDone"] if ts else 0; next_poll = time.time() + 0.5
                if status in (5, 6) or pct >= 1: break
            time.sleep(0.0005 if time.time() - t0 < 4 else 0.01)
        reqs = list(shared); sizes = [x[1] for x in samples if x[1] is not None]; first_samples = [x for x in samples if x[1] is not None][:5]
        jumps = [b - a for a, b in zip([0] + sizes[:-1], sizes)]
        print(f"=== case {case}: folder {folder}; sequentialDownload = {seq}; finished = {status in (5, 6) or pct >= 1} in {time.time() - t0:.1f} s")
        print("  the first 12 DIFFERENT pieces requested from the seed, in order (of %d):" % n, list(dict.fromkeys(reqs))[:12])
        print("  file size right after the first write: %.1f MiB (of %.0f MiB); size changes: %d; the biggest single jump: %.1f MiB" % ((sizes[0] if sizes else 0) / 1048576, SIZE / 1048576, len(sizes), (max(jumps) if jumps else 0) / 1048576))
        print("  first sizes (MiB):", [round(x / 1048576, 1) for x in sizes[:8]])
        print("  first samples (time s, size MiB, really allocated MiB):", [(a, round(b / 1048576, 2), round((c or 0) / 1048576, 2)) for a, b, c in first_samples])
        rpc("torrent-remove", ids=[t["id"] for t in rpc("torrent-get", fields=["id", "name"])["arguments"]["torrents"] if t["name"] == name], **{"delete-local-data": True})
        [x.stop() for x in seeders]; tracker.stop(); time.sleep(1)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2 /data/g31 /tmp/swarm_seed")
