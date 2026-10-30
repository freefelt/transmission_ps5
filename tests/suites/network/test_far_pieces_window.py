import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, base64, subprocess, time, os, shutil
from harness import *
import harness; harness.AUTO_FOLDER = False
import swarm
MiB = 1024 * 1024; PL = 1 * MiB; N = 600
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2; mkdir -p /mnt/usb2; mount -t tmpfs -o size=900m tmpfs /mnt/usb2")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    info, ih, path, n = swarm.make_torrent(bencode, "/tmp/swarm_seed", "FAR", N * PL, PL)
    # The only peers: one slow with ANY pieces and one fast that has only the FAR part (pieces 400..599): the nearest available pieces of the fast peer are 400 MiB away
    shared = []; slow = swarm.Seeder(path, PL, n, ih, rate_bps=10 * MiB, have=(0, n), log=shared); fast = swarm.Seeder(path, PL, n, ih, rate_bps=40 * MiB, have=(400, n), log=shared)
    tracker = swarm.Tracker([slow.port, fast.port])
    meta = base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tracker.port}/announce", "info": info})).decode()
    rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": "/mnt/usb2/torrents"}); t0 = time.time(); target = "/mnt/usb2/torrents/FAR.part"; sizes = []; done = False; nxt = 0; first_far = None
    while time.time() - t0 < 100 and not done:
        try: sizes.append(os.stat(target).st_size)
        except FileNotFoundError: pass
        if first_far is None and any(p >= 300 for p in shared): first_far = time.time() - t0
        if time.time() >= nxt:
            ts = [t for t in rpc("torrent-get", fields=["name", "status", "percentDone"])["arguments"]["torrents"] if t["name"] == "FAR"]; nxt = time.time() + 0.5
            done = bool(ts) and (ts[0]["status"] in (5, 6) or ts[0]["percentDone"] >= 1)
        time.sleep(0.002)
    order = list(dict.fromkeys(shared)); sizes = [s for i, s in enumerate(sizes) if i == 0 or s != sizes[i - 1]]; jumps = [b - a for a, b in zip([0] + sizes[:-1], sizes)]
    slow.stop(); fast.stop(); tracker.stop()
    check("it still downloads completely (the slow peer has everything; the fast one joins in when its far pieces come into the window)", done, round(time.time() - t0, 1))
    check("the first 150 requested pieces all lie within 230 pieces of the start (the window is 64 pieces ahead of the first missing one): the daemon does NOT jump to the pieces that only the fast peer has (400..599) while the beginning is still missing", all(p < 230 for p in order[:150]), [p for p in order[:150] if p >= 230][:5])
    check("the file never jumps far ahead: the biggest single growth step is at most 80 MiB (about the 64 MiB window; without the window it was 400 MiB at once, a minutes-long zero fill on exFAT)", sizes and max(jumps) <= 80 * MiB, round(max(jumps) / MiB, 1) if jumps else None)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2 /tmp/swarm_seed")
sys.exit(summary())
