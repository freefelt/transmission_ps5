import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, hashlib, base64, subprocess, time, os, shutil
from harness import *
import harness; harness.AUTO_FOLDER = False
OFF = "/data/Transmission/transmission/usbfix_off.txt"
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def meta(name): return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["id", "name", "downloadDir", "status", "hashString", "sequentialDownload", "sequential_download"])["arguments"]["torrents"] if t["name"] == name]
    if not ts: return None
    t = ts[0]; t["seq"] = t.get("sequentialDownload", t.get("sequential_download")); return t
def offlist(): return open(OFF).read().split() if os.path.exists(OFF) else []
def restart():
    api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped" and not dstate()["busy"], 45)
    for _ in range(20):
        if api("POST", "/api/daemon", {"action": "start"})[0] == 200: break
        time.sleep(1)
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80); time.sleep(3)
sh("umount /mnt/usb2 /mnt/ext1 2>/dev/null; rm -rf /mnt/usb2 /mnt/ext1; mkdir -p /mnt/usb2 /mnt/ext1; mount -t tmpfs -o size=64m tmpfs /mnt/usb2; mount -t tmpfs -o size=64m tmpfs /mnt/ext1")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True}); api("POST", "/api/folders", {"action": "add", "path": "/mnt/ext1/torrents"})
    U = "/mnt/usb2/torrents"; E = "/mnt/ext1/torrents"
    def add(name, folder, fix=None, paused=False):
        a = {"metainfo": meta(name), "paused": paused, "download-dir": folder}
        if fix is not None: a["ps5_usb_fix"] = fix
        return rpc("torrent-add", **a)
    add("A_default", U); add("B_off", U, False); add("C_on", U, True); add("D_off_m2", E, False); add("E_m2", E); time.sleep(2)
    t = {n: tget(n) for n in ("A_default", "B_off", "C_on", "D_off_m2", "E_m2")}
    check("a USB torrent added WITHOUT the parameter (e.g. by another RPC client) gets the fix by default (sequential mode on)", t["A_default"]["seq"] is True, t["A_default"]["seq"])
    check("a USB torrent added with ps5_usb_fix=false has the fix OFF (normal order) and is listed in usbfix_off.txt by its hash", t["B_off"]["seq"] is False and t["B_off"]["hashString"] in offlist(), (t["B_off"]["seq"], offlist()))
    check("a USB torrent added with ps5_usb_fix=true has the fix ON and is not in the off list", t["C_on"]["seq"] is True and t["C_on"]["hashString"] not in offlist(), (t["C_on"]["seq"], t["C_on"]["hashString"] in offlist()))
    check("a torrent on the M.2 folder is never switched to sequential (the fix is only for USB disks)", t["E_m2"]["seq"] is False and t["D_off_m2"]["seq"] is False, (t["E_m2"]["seq"], t["D_off_m2"]["seq"]))
    print("== the choice survives a daemon restart (it is not lost like a plain flag would be)")
    restart(); t = {n: tget(n) for n in t}
    check("after the restart: A on, B still OFF, C on (the explicit choice of the user is remembered)", (t["A_default"]["seq"], t["B_off"]["seq"], t["C_on"]["seq"]) == (True, False, True), (t["A_default"]["seq"], t["B_off"]["seq"], t["C_on"]["seq"]))
    print("== moving torrents")
    for n in ("D_off_m2", "E_m2"): rpc("torrent-set-location", ids=[t[n]["id"]], location=U, move=False)
    time.sleep(2); t = {n: tget(n) for n in t}
    check("a torrent moved onto the USB disk gets the fix (E: yes) unless the user turned it off for that torrent (D: stays off)", t["E_m2"]["seq"] is True and t["D_off_m2"]["seq"] is False, (t["E_m2"]["seq"], t["D_off_m2"]["seq"]))
    print("== adding the same torrent again with the fix ON")
    h = t["B_off"]["hashString"]; rpc("torrent-remove", ids=[t["B_off"]["id"]], **{"delete-local-data": True}); time.sleep(1)
    add("B_off", U, True); time.sleep(2); b = tget("B_off")
    check("re-adding the same torrent with the fix ON removes it from the off list and turns the fix on", b["seq"] is True and h not in offlist(), (b["seq"], h in offlist()))
    log = open("/data/Transmission/logs/daemon.log", encoding="utf-8", errors="replace").read()
    check("the choice is written to the daemon log ('usb-fix: OFF ...' / 'usb-fix: ON ...')", "usb-fix: OFF (chosen by the user when adding)" in log and "usb-fix: ON for torrent" in log, [l for l in log.split("\n") if "usb-fix" in l][:3])
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 /mnt/ext1 2>/dev/null; rm -rf /mnt/usb2 /mnt/ext1")
sys.exit(summary())
