# Makes the screenshots of the README (docs/images/<lang>-*.png): the real manager and daemon, folders that look like a console
# (big tmpfs disks), realistic torrents (sparse files, plus one download that really runs from a throttled local seeder),
# then shots.js takes the pictures in Chrome.
# Like every test: Linux with root, in a VM (it mounts /mnt/ext1 and /mnt/usb0). Prepare as for the tests (README, "Tests"), then:
#   sudo python3 tests/screenshots/make_screenshots.py en docs/images
#   sudo python3 tests/screenshots/make_screenshots.py ru docs/images
import os, sys, time, shutil, subprocess, hashlib, base64
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, REPO + "/tests/lib")
import harness
harness.TEST_MSG = None; harness.TEST_LANG = None; harness.AUTO_FOLDER = False
from harness import *
import swarm
LANG, OUT = sys.argv[1], sys.argv[2]
os.environ["PS5TM_TEST_MOUNTINFO"] = "/mnt/ext1=bfs=/dev/ssd1.user;/mnt/usb0=ufs=/dev/es0.crypt"
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
for d, size in (("ext1", "1863g"), ("usb0", "953g")):   # real separate file systems (the guard wants that); sparse files cost nothing
    sh(f"umount /mnt/{d} 2>/dev/null"); os.makedirs(f"/mnt/{d}", exist_ok=True); sh(f"mount -t tmpfs -o size={size} tmpfs /mnt/{d}")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
PL = 4 * 1024 * 1024
def sparse(name, folder, size, have_frac, paused=False):
    n = -(-size // PL); have = int(n * have_frac)
    os.makedirs(folder, exist_ok=True)
    with open(f"{folder}/{name}", "wb") as f: f.truncate(size)
    zero = hashlib.sha1(b"\0" * PL).digest(); last = size - (n - 1) * PL; zlast = hashlib.sha1(b"\0" * last).digest()
    pieces = b"".join((zlast if i == n - 1 else zero) if i < have else hashlib.sha1(b"x" + str(i).encode()).digest() for i in range(n))
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": size, "piece length": PL, "pieces": pieces}})).decode()
    rpc("torrent-add", metainfo=meta, paused=paused, **{"download-dir": folder})
GB = 1024 ** 3
fresh(); os.makedirs("/data/Transmission", exist_ok=True)
with open("/data/Transmission/config.cfg", "a", encoding="utf-8") as cf: cf.write(f"language={LANG}\n")
loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    api("POST", "/api/config", {"language": LANG}); time.sleep(1)
    rpc("session-set", **{"utp-enabled": False, "dht-enabled": False, "lpd-enabled": False, "pex-enabled": False, "encryption": "tolerated"})
    for p in ("/mnt/ext1/Torrents", "/mnt/usb0/Torrents"):
        c, t = api("POST", "/api/folders", {"action": "add", "path": p, "confirm": True}); print("folder", p, c, t[:80])
    E1, U0 = "/mnt/ext1/Torrents", "/mnt/usb0/Torrents"
    sparse("ubuntu-24.04.1-desktop-amd64.iso", E1, int(5.9 * GB), 1.0)
    sparse("debian-12.7.0-amd64-DVD-1.iso", E1, int(3.7 * GB), 0.42)
    sparse("archlinux-2024.10.01-x86_64.iso", U0, int(1.2 * GB), 1.0)
    sparse("Fedora-Workstation-Live-40-1.14.x86_64.iso", U0, int(2.2 * GB), 0.78, paused=True)
    sparse("linuxmint-22-cinnamon-64bit.iso", E1, int(2.9 * GB), 1.0, paused=True)
    # a download that really runs (a throttled local seeder), so the speed and the ETA are on the screen
    info, ih, path, n = swarm.make_torrent(bencode, "/tmp/swarm_seed", "opensuse-tumbleweed-dvd-x86_64.iso", 1400 * 1024 * 1024, 1024 * 1024)
    seeder = swarm.Seeder(path, 1024 * 1024, n, ih, rate_bps=7 * 1024 * 1024); tracker = swarm.Tracker([seeder.port])
    meta = base64.b64encode(bencode({"announce": f"http://127.0.0.1:{tracker.port}/announce", "info": info})).decode()
    rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": E1})
    time.sleep(25)
    print("torrents:", [(t["name"][:24], t["percentDone"], t["rateDownload"]) for t in rpc("torrent-get", fields=["name", "percentDone", "rateDownload"])["arguments"]["torrents"]], flush=True)
    try: os.rmdir("/mnt/ext1/torrents")
    except OSError: pass
    r = subprocess.run(["node", os.path.dirname(os.path.abspath(__file__)) + "/shots.js", LANG, os.path.abspath(OUT)], capture_output=True, text=True, timeout=240)
    print(r.stdout[-1500:], r.stderr[-800:])
    seeder.stop(); tracker.stop()
finally:
    kill_all(); loader.close()
    for d in ("ext1", "usb0"): sh(f"umount /mnt/{d} 2>/dev/null")
