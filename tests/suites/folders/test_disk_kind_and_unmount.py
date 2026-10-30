import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, time, os, subprocess, shutil, base64, hashlib
# Drive kind by device, file system, "USB fix", warnings when adding a folder, unmounting (busy, who holds it, forced)
os.environ["PS5TM_TEST_MOUNTINFO"] = "/mnt/ext0=ufs=/dev/es0.crypt;/mnt/usb2=msdosfs=/dev/da1p1;/mnt/usb3=exfatfs=/dev/da2p1;/mnt/ext1=bfs=/dev/ssd1.user"
from harness import *
import harness; harness.AUTO_FOLDER = False
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def meta(name): return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def fpost(**b): c, t = api("POST", "/api/folders", b); return c, (json.loads(t) if t.startswith("{") else {"raw": t})
def folders(): return json.loads(api("GET", "/api/folders")[1])
def mounted(p): return any(l.split()[1] == p for l in open("/proc/self/mounts"))
def jrpc(method, params=None):
    sid = ""
    for _ in range(3):
        req = urllib.request.Request("http://127.0.0.1:9091/transmission/rpc", data=json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}).encode(), headers={"Content-Type": "application/json", "X-Transmission-Session-Id": sid})
        try: return json.loads(urllib.request.urlopen(req, timeout=10).read())
        except urllib.error.HTTPError as e:
            if e.code == 409: sid = e.headers.get("X-Transmission-Session-Id"); continue
            return json.loads(e.read() or b"{}")
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["id", "name", "status", "sequentialDownload", "sequential_download"])["arguments"]["torrents"] if t["name"] == name]
    if not ts: return None
    t = ts[0]; t["sequentialDownload"] = t.get("sequentialDownload", t.get("sequential_download")); return t
DISKS = ("/mnt/ext0", "/mnt/ext1", "/mnt/usb2", "/mnt/usb3")
def cleanup():
    for d in DISKS: sh(f"umount -l {d} 2>/dev/null; rm -rf {d}")
    sh("rm -rf /data/g48")
cleanup(); shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
for d in DISKS: os.makedirs(d, exist_ok=True); sh(f"mount -t tmpfs -o size=64m tmpfs {d}")
os.makedirs("/data/g48", exist_ok=True)
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    j = folders(); cand = {c["root"]: c for c in j["candidates"]}
    check("candidates: the kind comes from the DEVICE: /mnt/ext0 (es0.crypt) is a USB drive, /mnt/ext1 (ssd1.user) is M.2, /mnt/usb2 and /mnt/usb3 (da*) are USB", [cand[r]["kind"] for r in DISKS] == ["usb", "ssd", "usb", "usb"], {r: cand[r]["kind"] for r in cand})
    check("candidates carry the file system name and whether it needs the fix: UFS and BFS do not, FAT32 and exFAT do", [(cand[r]["fs"], cand[r]["fsbad"]) for r in DISKS] == [("UFS", False), ("BFS", False), ("FAT32", True), ("exFAT", True)], {r: (cand[r]["fs"], cand[r]["fsbad"]) for r in cand})
    print("== warnings when a folder is added")
    c, r = fpost(action="add", path="/mnt/usb3/torrents")
    check("exFAT: 409, need_confirm with the reason 'fs' and the file system name 'exFAT' (the page shows the explanation)", c == 409 and r.get("need_confirm") and r.get("reason") == "fs" and r.get("fs") == "exFAT", (c, r))
    c, r = fpost(action="add", path="/mnt/usb2/torrents")
    check("FAT32: the same, with the name 'FAT32'", c == 409 and r.get("reason") == "fs" and r.get("fs") == "FAT32", (c, r))
    c, r = fpost(action="add", path="/data/g48")
    check("an internal disk keeps its own reason ('int')", c == 409 and r.get("reason") == "int", (c, r))
    c1, _ = fpost(action="add", path="/mnt/ext0/torrents"); c2, _ = fpost(action="add", path="/mnt/ext1/torrents")
    check("UFS (extended storage on USB) and the M.2 are added without any warning", c1 == 200 and c2 == 200, (c1, c2))
    c1, _ = fpost(action="add", path="/mnt/usb3/torrents", confirm=True); c2, _ = fpost(action="add", path="/mnt/usb2/torrents", confirm=True)
    check("after the confirmation the exFAT and FAT32 folders are added", c1 == 200 and c2 == 200, (c1, c2))
    rows = {f["path"]: f for f in folders()["folders"]}
    check("rows: ext0 is 'usb' (UFS, not bad, device /dev/es0.crypt), usb3 is 'usb' (exFAT, bad), ext1 is 'ssd'", rows["/mnt/ext0/torrents"]["kind"] == "usb" and rows["/mnt/ext0/torrents"]["fsbad"] is False and rows["/mnt/ext0/torrents"]["device"] == "/dev/es0.crypt" and rows["/mnt/usb3/torrents"]["fsbad"] is True and rows["/mnt/usb3/torrents"]["fs"] == "exFAT" and rows["/mnt/ext1/torrents"]["kind"] == "ssd", {p: (f["kind"], f["fs"], f["fsbad"]) for p, f in rows.items()})
    print("== the USB fix follows the file system")
    names = {"T_ufs": "/mnt/ext0/torrents", "T_ssd": "/mnt/ext1/torrents", "T_fat": "/mnt/usb2/torrents", "T_exfat": "/mnt/usb3/torrents"}
    for n, d in names.items(): rpc("torrent-add", metainfo=meta(n), paused=True, **{"download-dir": d})
    for n in names: rpc("torrent-start", ids=[tget(n)["id"]])
    time.sleep(3)
    r = {n: tget(n)["sequentialDownload"] for n in names}
    check("started: sequential (the fix) ONLY on FAT32 and exFAT; the UFS USB drive (/mnt/ext0) and the M.2 keep the normal order", r == {"T_ufs": False, "T_ssd": False, "T_fat": True, "T_exfat": True}, r)
    pf = jrpc("ps5_folders").get("result", {}).get("folders", [])
    fsmap = {f["path"]: (f.get("fs"), f.get("fsbad")) for f in pf}
    check("the folder list RPC for the web client carries the file system too (for the red 'not recommended (exFAT)' label of the add dialog): UFS and BFS are fine, FAT32 and exFAT are marked", fsmap.get("/mnt/ext0/torrents") == ("UFS", False) and fsmap.get("/mnt/ext1/torrents") == ("BFS", False) and fsmap.get("/mnt/usb2/torrents") == ("FAT32", True) and fsmap.get("/mnt/usb3/torrents") == ("exFAT", True), fsmap)
    check("the folder list RPC for the web client gives the kind by device too (ext0 is usb)", {f["path"]: f["kind"] for f in pf}.get("/mnt/ext0/torrents") == "usb" and {f["path"]: f["kind"] for f in pf}.get("/mnt/ext1/torrents") == "ssd", {f["path"]: f["kind"] for f in pf})
    print("== unmount: busy, who holds it, forced")
    hold = open("/mnt/usb3/hold.txt", "w"); hold.write("x"); hold.flush()
    t0 = time.time(); c, r = fpost(action="unmount", path="/mnt/usb3/torrents")
    h = r.get("holders", [])
    check("a busy drive: 409 with busy=true, the message and the list of holders (the process, pid, number of open files, a sample path); nothing is forced", c == 409 and r.get("busy") is True and "занят" in r.get("error", "") and mounted("/mnt/usb3") and time.time() - t0 >= 2 and any(x["pid"] == os.getpid() and x["count"] >= 1 and x["sample"].startswith("/mnt/usb3") and x["kind"] == "app" for x in h), (c, r.get("error", "")[:80], h[:3]))
    check("the torrents of that disk were stopped before the attempt", tget("T_exfat")["status"] == 0, tget("T_exfat")["status"])
    c, r = fpost(action="unmount", path="/mnt/usb3/torrents", force=True)
    check("the forced eject (sync, then a forced unmount) works while the file is still open; the message is short and calm: 'USB-диск /mnt/usb3 извлечён'", c == 200 and r.get("ok") and r.get("message") == "USB-диск /mnt/usb3 извлечён" and not mounted("/mnt/usb3"), (c, r))
    hold.close()
    c, r = fpost(action="unmount", path="/mnt/ext0/torrents")
    check("the extended storage of the console (/mnt/ext0, es0.crypt) is NOT unmounted the ordinary way when the system cannot be asked (the console would think that the drive was disconnected wrongly): 409 with the advice to use 'Безопасно удалить из PS5'; the mount stays", c == 409 and "Безопасно удалить из PS5" in r.get("error", "") and mounted("/mnt/ext0"), (c, r))
    c, r = fpost(action="unmount", path="/mnt/ext1/torrents")
    check("the M.2 SSD cannot be unmounted (only USB drives)", c == 409 and "только USB-диск" in r.get("error", "") and mounted("/mnt/ext1"), (c, r))
    c, r = fpost(action="unmount", path="/mnt/usb3/torrents")
    check("a drive that is already gone: 'the drive is not connected'", c == 409 and "Диск не подключён" in r.get("error", ""), (c, r))
    c, r = fpost(action="unmount", path="/mnt/usb2/torrents")
    check("a free FAT32 drive unmounts in the normal way (no force)", c == 200 and r.get("ok") and "извлечён принудительно" not in r.get("message", "") and "размонтирован" in r.get("message", "") and not mounted("/mnt/usb2"), (c, r))
    print("== the system eject (on the host a stub switched by /tmp/ps5tm_test_eject: ok | busy | gone | fail)")
    EJ = "/tmp/ps5tm_test_eject"
    def hook(mode):
        if mode: open(EJ, "w").write(mode)
        elif os.path.exists(EJ): os.remove(EJ)
    try: os.remove(EJ + ".log")
    except FileNotFoundError: pass
    hook("fail"); c, r = fpost(action="unmount", path="/mnt/ext0/torrents")
    check("the system refuses (an error): the extended storage stays mounted, the message says that the system could not disconnect it", c == 409 and not r.get("busy") and "Система не смогла отключить" in r.get("error", "") and mounted("/mnt/ext0"), (c, r))
    hook("busy"); c, r = fpost(action="unmount", path="/mnt/ext0/torrents")
    check("the system says 'busy': the extended storage is not forced, a plain 'drive is busy' message, the mount stays", c == 409 and not r.get("busy") and "занят" in r.get("error", "") and mounted("/mnt/ext0"), (c, r))
    c, r = fpost(action="unmount", path="/mnt/ext0/torrents", force=True)
    check("a forced eject of the extended storage is refused: only the system may disconnect it", c == 409 and "принудительно" in r.get("error", "") and mounted("/mnt/ext0"), (c, r))
    hook("ok"); c, r = fpost(action="unmount", path="/mnt/ext0/torrents")
    check("the system disconnects the extended storage: 200, the message says so (and that games and apps on it are unavailable), the mount is gone, and the request named the mount point", c == 200 and r.get("ok") and "отключено системой" in r.get("message", "") and not mounted("/mnt/ext0") and "eject /mnt/ext0" in open(EJ + ".log").read(), (c, r))
    sh("mount -t tmpfs -o size=64m tmpfs /mnt/usb2")
    hook("ok"); c, r = fpost(action="unmount", path="/mnt/usb2/torrents")
    check("an ordinary USB drive goes through the system too: 200 and the mount is gone", c == 200 and r.get("ok") and "/mnt/usb2" in r.get("message", "") and not mounted("/mnt/usb2") and open(EJ + ".log").read().count("eject /mnt/usb2") == 1, (c, r))
    sh("mount -t tmpfs -o size=64m tmpfs /mnt/usb2")
    hook("busy"); c, r = fpost(action="unmount", path="/mnt/usb2/torrents")
    check("an ordinary USB drive that the system finds busy: 409 with busy=true and the list of holders (the forced way stays available); the mount stays", c == 409 and r.get("busy") is True and "holders" in r and mounted("/mnt/usb2"), (c, r))
    hook("gone"); c, r = fpost(action="unmount", path="/mnt/usb2/torrents")
    check("the system does not know the drive (not found): an ordinary USB drive falls back to the ordinary unmount and is unmounted", c == 200 and r.get("ok") and not mounted("/mnt/usb2"), (c, r))
    hook(None)
finally:
    kill_all(); cleanup()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
