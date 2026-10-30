import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, glob, hashlib, base64, subprocess
from harness import *
import harness; harness.AUTO_FOLDER = False
S = "/data/Transmission/transmission/config/settings.json"; NO = "/dev/null/ps5tm-no-download-folder"
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def jread(): return json.load(open(S, encoding="utf-8"))
def post(**b): return api("POST", "/api/folders", b)[0]
def sget(k): return rpc("session-get", fields=[k])["arguments"].get(k)
def stop_daemon():
    api("POST", "/api/daemon", {"action": "stop"})
    wait(lambda: dstate()["state"] == "stopped" and not dstate()["busy"], 45)   # the manager is free now: otherwise the next "start" request is refused
def start_daemon():
    for _ in range(20):
        c, _t = api("POST", "/api/daemon", {"action": "start"})
        if c == 200: break
        time.sleep(1)   # the manager is still busy with the previous action: retry
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80)
def daemon_cycle(mutate=None, kill_state=None):
    stop_daemon()
    if mutate:
        d = jread(); mutate(d); json.dump(d, open(S, "w", encoding="utf-8"), indent=4, ensure_ascii=False)
    if kill_state: kill_state()
    start_daemon(); time.sleep(2)
    return jread()
def meta(name):
    return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["id", "name", "downloadDir", "status", "errorString", "hashString"])["arguments"]["torrents"] if t["name"] == name]; return ts[0] if ts else None
def cleanup():
    for d in ("/mnt/usb2", "/mnt/ext1"): sh(f"umount {d} 2>/dev/null; rm -rf {d}")
cleanup(); shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
for d in ("/mnt/usb2", "/mnt/ext1"): os.makedirs(d, exist_ok=True)
sh("mount -t tmpfs -o size=64m tmpfs /mnt/usb2"); sh("mount -t tmpfs -o size=64m tmpfs /mnt/ext1")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    print("== the list of download folders is EMPTY: there is nothing there")
    check("an empty list: Transmission's own default folder is an UNUSABLE placeholder (not /mnt/ext1/torrents, not /data/torrents), and the interface shows an empty folder", jread()["download-dir"] == NO and sget("download-dir") == "", (jread()["download-dir"], sget("download-dir")))
    print("== only a USB folder is in the list")
    check("one USB folder is added", post(action="add", path="/mnt/usb2/torrents", confirm=True) == 200, None)
    d = daemon_cycle()
    check("after a daemon restart the default of Transmission is STILL the placeholder (a path is never silently replaced by another valid folder); the interface shows the folder of the list", d["download-dir"] == NO and sget("download-dir") == "/mnt/usb2/torrents", (d["download-dir"], sget("download-dir")))
    r = rpc("torrent-add", metainfo=meta("T_nodir"), paused=True)
    check("a torrent added without a folder goes to the list's folder", r["result"] == "success" and tget("T_nodir")["downloadDir"] == "/mnt/usb2/torrents", tget("T_nodir"))
    print("== a default folder chosen on purpose (it is in the list) is kept, and only while it is in the list")
    post(action="add", path="/mnt/ext1/Games")
    check("a folder of the list is chosen as the default", rpc("session-set", **{"download-dir": "/mnt/ext1/Games"})["result"] == "success", None)
    d = daemon_cycle()
    check("it is kept after the restart", d["download-dir"] == "/mnt/ext1/Games" and sget("download-dir") == "/mnt/ext1/Games", d["download-dir"])
    d = daemon_cycle(mutate=lambda x: x.update({"download-dir": "/MNT/ext1/games/"}))
    check("a different spelling of a folder of the list is left exactly as it is (nothing is rewritten)", d["download-dir"] == "/MNT/ext1/games/", d["download-dir"])
    post(action="remove", path="/mnt/ext1/Games"); d = daemon_cycle()
    check("when the chosen default leaves the list it becomes the placeholder at the next start (not another folder)", d["download-dir"] == NO, d["download-dir"])
    d = daemon_cycle(mutate=lambda x: x.update({"download-dir": '/mnt/ext1/odd "quoted" \\\\ path'}))
    check("an odd path with quotes and backslashes: the file stays valid JSON and the path becomes the placeholder", d["download-dir"] == NO, d["download-dir"])
    print("== a torrent whose saved state is gone: it does NOT land in any other folder")
    post(action="add", path="/mnt/ext1/Games")
    r = rpc("torrent-add", metainfo=meta("T_lost"), paused=True, **{"download-dir": "/mnt/ext1/Games"}); hs = tget("T_lost")["hashString"]
    def lose_resume():
        for f in glob.glob("/data/Transmission/transmission/config/resume/*"):
            if hs[:16] in os.path.basename(f) or hs in os.path.basename(f): os.remove(f)
    d = daemon_cycle(kill_state=lose_resume); t = tget("T_lost")
    check("it comes back PAUSED with the placeholder as its folder (not the folder of the list, not any real folder)", t is not None and t["status"] == 0 and t["downloadDir"].startswith(NO), t)
    rpc("torrent-start", ids=[t["id"]]); time.sleep(2); t = tget("T_lost")
    check("it cannot be started: it stays stopped with a clear message telling to use 'Set location'", t["status"] == 0 and "Set location" in t["errorString"] and "is unknown" in t["errorString"], t["errorString"])
    r = rpc("torrent-set-location", ids=[t["id"]], location="/mnt/ext1/Games", move=False); time.sleep(1); t = tget("T_lost")
    check("'Set location' to a folder of the list fixes it: its folder is that one", r["result"] == "success" and t["downloadDir"] == "/mnt/ext1/Games", (r["result"], t["downloadDir"]))
    rpc("torrent-start", ids=[t["id"]]); time.sleep(2); t = tget("T_lost")
    check("and now it starts", t["status"] != 0 and "Set location" not in t["errorString"], (t["status"], t["errorString"]))
    check("the daemon works after all that", rpc_ok(), None)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    cleanup()
sys.exit(summary())
