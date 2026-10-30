import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, glob, hashlib, base64, signal, subprocess
from harness import *
import harness; harness.AUTO_FOLDER = False
RES = "/data/Transmission/transmission/config/resume"
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def meta(name): return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["id", "name", "downloadDir", "status", "hashString"])["arguments"]["torrents"] if t["name"] == name]; return ts[0] if ts else None
def resume_of(h): return [f for f in glob.glob(RES + "/*") if h in os.path.basename(f)]
def kill_restart():
    pid = daemon_pids()[0]; os.kill(pid, signal.SIGKILL)
    wait(lambda: daemon_pids() and daemon_pids()[0] != pid and rpc_ok() and dstate()["state"] == "running", 80); time.sleep(3)
sh("umount /mnt/usb2 /mnt/ext1 2>/dev/null; rm -rf /mnt/usb2 /mnt/ext1; mkdir -p /mnt/usb2 /mnt/ext1; mount -t tmpfs -o size=64m tmpfs /mnt/usb2; mount -t tmpfs -o size=64m tmpfs /mnt/ext1")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/ext1/torrents"}); api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    rpc("session-set", **{"download-dir": "/mnt/ext1/torrents"})
    print("== the state of a NEW torrent is saved at once (it used to appear only after 6 minutes)")
    rpc("torrent-add", metainfo=meta("T_new"), paused=False, **{"download-dir": "/mnt/usb2/torrents"}); h = tget("T_new")["hashString"]
    check("right after adding there is already a state file (resume) for the new torrent", len(resume_of(h)) == 1 and os.path.getsize(resume_of(h)[0]) > 50, resume_of(h))
    print("== the case from the console: the daemon is killed a few seconds after the torrent was added to a USB folder")
    kill_restart(); t = tget("T_new")
    check("after the forced kill and restart the torrent is still in the folder it was ADDED to (/mnt/usb2/torrents), not in the default one (/mnt/ext1/torrents)", t is not None and t["downloadDir"] == "/mnt/usb2/torrents", t)
    print("== a change of the folder is saved within 20 s (it used to take up to 6 minutes)")
    rpc("torrent-add", metainfo=meta("T_move"), paused=True, **{"download-dir": "/mnt/usb2/torrents"}); t = tget("T_move")
    r = rpc("torrent-set-location", ids=[t["id"]], location="/mnt/ext1/torrents", move=False)
    h2 = t["hashString"]; ok = wait(lambda: any(b"/mnt/ext1/torrents" in open(f, "rb").read() for f in resume_of(h2)), 30)
    check("the new folder reaches the state file within 30 s", r["result"] == "success" and ok, (r["result"], ok))
    kill_restart(); t = tget("T_move")
    check("after another forced kill the moved torrent keeps its NEW folder", t is not None and t["downloadDir"] == "/mnt/ext1/torrents", t)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 /mnt/ext1 2>/dev/null; rm -rf /mnt/usb2 /mnt/ext1")
sys.exit(summary())
