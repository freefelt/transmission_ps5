import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, hashlib, base64, subprocess
from harness import *
import harness; harness.AUTO_FOLDER = False
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def post(**b): return api("POST", "/api/folders", b)[0]
def meta(name): return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["id", "name", "downloadDir", "status", "sequentialDownload", "sequential_download"])["arguments"]["torrents"] if t["name"] == name]
    if not ts: return None
    t = ts[0]; t["sequentialDownload"] = t.get("sequentialDownload", t.get("sequential_download")); return t
def cleanup():
    for d in ("/mnt/usb2", "/mnt/ext1"): sh(f"umount {d} 2>/dev/null; rm -rf {d}")
    sh("rm -rf /data/g29")
cleanup(); shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
for d in ("/mnt/usb2", "/mnt/ext1"): os.makedirs(d, exist_ok=True)
sh("mount -t tmpfs -o size=64m tmpfs /mnt/usb2"); sh("mount -t tmpfs -o size=64m tmpfs /mnt/ext1")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    check("three folders: USB, M.2, internal", [post(action="add", path="/mnt/usb2/torrents", confirm=True), post(action="add", path="/mnt/ext1/Games"), api("POST", "/api/folders", {"action": "add", "path": "/data/g29", "confirm": True})[0]] == [200, 200, 200], None)
    for name, d in (("T_usb", "/mnt/usb2/torrents"), ("T_m2", "/mnt/ext1/Games"), ("T_int", "/data/g29")):
        rpc("torrent-add", metainfo=meta(name), paused=True, **{"download-dir": d})
    check("a torrent that was only ADDED (paused, not started yet) is not switched to sequential yet", all(tget(n)["sequentialDownload"] is False for n in ("T_usb", "T_m2", "T_int")), [tget(n)["sequentialDownload"] for n in ("T_usb", "T_m2", "T_int")])
    for n in ("T_usb", "T_m2", "T_int"): rpc("torrent-start", ids=[tget(n)["id"]])
    time.sleep(3)
    r = {n: tget(n)["sequentialDownload"] for n in ("T_usb", "T_m2", "T_int")}
    check("when started: the torrent on the USB disk (/mnt/usbN) is downloaded IN ORDER (sequential), M.2 and the internal disk keep the normal order", r == {"T_usb": True, "T_m2": False, "T_int": False}, r)
    log = open("/data/Transmission/logs/daemon.log", encoding="utf-8", errors="replace").read()
    check("every start is written to the daemon log with the folder: 'torrent start: <name> -> <folder>'", "torrent start: T_usb -> /mnt/usb2/torrents" in log and "torrent start: T_m2 -> /mnt/ext1/Games" in log and "torrent start: T_int -> /data/g29" in log, [l for l in log.split("\n") if "torrent start" in l][:4])
    api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped", 45)
    api("POST", "/api/daemon", {"action": "start"}); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80); time.sleep(3)
    r = {n: tget(n)["sequentialDownload"] for n in ("T_usb", "T_m2", "T_int")}
    check("after a daemon restart the USB torrent is still sequential, the others are not", r == {"T_usb": True, "T_m2": False, "T_int": False}, r)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    cleanup()
sys.exit(summary())
