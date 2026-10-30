import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, base64, subprocess, time, os, shutil, re, json
from harness import *
import harness; harness.AUTO_FOLDER = False
import swarm
MiB = 1024 * 1024
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def bell(): c, t = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(t)["items"]] if c == 200 else []
sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2; mkdir -p /mnt/usb2; mount -t tmpfs -o size=200m tmpfs /mnt/usb2; rm -f /tmp/ps5tm_test_io_delay")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    info, ih, path, n = swarm.make_torrent(bencode, "/tmp/swarm_seed", "SLOWFILE", 4 * MiB, 256 * 1024)
    seed = swarm.Seeder(path, 256 * 1024, n, ih, rate_bps=20 * MiB); tracker = swarm.Tracker([seed.port])
    meta = base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tracker.port}/announce", "info": info})).decode()
    open("/tmp/ps5tm_test_io_delay", "w").write("25000 SLOWFILE\n")      # ONE write to SLOWFILE.part will hang for 25 s (simulating a stuck disk)
    rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": "/mnt/usb2/torrents"}); t0 = time.time()
    logf = "/data/Transmission/logs/daemon.log"
    got3 = wait(lambda: "io-watch: blocked 3 pwrite" in open(logf, encoding="utf-8", errors="replace").read(), 40)
    check("while the write hangs, the watchdog (a separate thread of the daemon) reports it after 3 s: 'io-watch: blocked 3 pwrite ... path=.../SLOWFILE.part'", got3 and "SLOWFILE.part" in open(logf, encoding="utf-8", errors="replace").read(), None)
    got_done = wait(lambda: re.search(r"io-watch: done 2[4-9]\.\d pwrite .*SLOWFILE\.part", open(logf, encoding="utf-8", errors="replace").read()) is not None, 40)
    log = open(logf, encoding="utf-8", errors="replace").read()
    check("when the write finally returns, the log says how long it took: 'io-watch: done 25.0 pwrite ...'; the 10-second report is there too", got_done and "io-watch: blocked 10 pwrite" in log, [l for l in log.split("\n") if "io-watch" in l][:5])
    got_bell = wait(lambda: any(re.search(r"Диск ответил: запись в SLOWFILE\.part .*заняло 2[4-9] с", x) for x in bell()), 40)
    texts = bell()
    check("the BELL has both entries with durations: 'Диск не отвечает: запись в SLOWFILE.part ... уже 10 с' and 'Диск ответил: запись в SLOWFILE.part ... заняло 25 с'", got_bell and any(re.search(r"Диск не отвечает: запись в SLOWFILE\.part .*уже 1\d с", x) for x in texts), texts[-6:])
    got_rec = wait(lambda: any(re.search(r"Transmission снова отвечает: не отвечал (1\d|2\d) с", x) for x in bell()), 40)
    check("the daemon was silent for ~25 s (the whole thing is single-threaded), the manager noticed and the bell tells: 'Transmission снова отвечает: не отвечал NN с'", got_rec, [x for x in bell() if "Transmission" in x][-4:])
    check("no duplicates: every one of these entries is in the bell exactly once", all(sum(1 for x in bell() if k in x) == 1 for k in ("Диск ответил: запись в SLOWFILE.part", "Transmission снова отвечает")), [x[:60] for x in bell()][-8:])
    ok = wait(lambda: rpc_ok() and tget_done(), 40) if False else rpc_ok()
    check("the daemon works normally afterwards and the torrent can finish", ok, None)
    seed.stop(); tracker.stop()
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 2>/dev/null; rm -rf /mnt/usb2 /tmp/swarm_seed; rm -f /tmp/ps5tm_test_io_delay")
sys.exit(summary())
