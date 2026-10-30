import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, subprocess, time, os, shutil, urllib.request, urllib.error
from harness import *
import harness; harness.AUTO_FOLDER = False
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def jrpc(method, params=None):
    sid = ""
    for _ in range(3):
        req = urllib.request.Request("http://127.0.0.1:9091/transmission/rpc", data=json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}).encode(), headers={"Content-Type": "application/json", "X-Transmission-Session-Id": sid})
        try: return json.loads(urllib.request.urlopen(req, timeout=10).read())
        except urllib.error.HTTPError as e:
            if e.code == 409: sid = e.headers.get("X-Transmission-Session-Id"); continue
            return json.loads(e.read() or b"{}")
sh("umount /mnt/usb2 /mnt/usb3 /mnt/ext1 2>/dev/null; rm -rf /mnt/usb2 /mnt/usb3 /mnt/ext1 /data/g37; mkdir -p /mnt/usb2 /mnt/usb3 /mnt/ext1; for d in usb2 usb3 ext1; do mount -t tmpfs -o size=64m tmpfs /mnt/$d; done")
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    for p in ("/mnt/ext1/torrents", "/mnt/usb2/torrents", "/mnt/usb3/torrents"): api("POST", "/api/folders", {"action": "add", "path": p, "confirm": True})
    api("POST", "/api/folders", {"action": "add", "path": "/data/g37", "confirm": True})
    sh("umount /mnt/usb3")                      # the drive that is "unplugged" later: the folder stays in the list
    r = jrpc("ps5_folders"); f = r.get("result", {}).get("folders", [])
    check("the RPC method ps5_folders exists and returns the list of download folders in the order of the list", [x["path"] for x in f] == ["/mnt/ext1/torrents", "/mnt/usb2/torrents", "/mnt/usb3/torrents", "/data/g37"], [x.get("path") for x in f] if f else r)
    check("every entry has path, kind, mounted, total, free and the file system (fs, fsbad: exFAT is marked, the internal disk has none); kind is ssd for /mnt/extN, usb for /mnt/usbN, int for the internal disk", [x["kind"] for x in f] == ["ssd", "usb", "usb", "int"] and all(set(x) == {"path", "kind", "mounted", "total", "free", "fs", "fsbad"} for x in f) and [(x["fs"], x["fsbad"]) for x in f] == [("BFS", False), ("FAT32" if False else "exFAT", True), ("exFAT", True), ("", False)], [(x["path"], x["kind"]) for x in f])
    check("connected disks: mounted is true, total is the real capacity (the 64 MiB test disk), free is not more than total", f[0]["mounted"] and f[1]["mounted"] and f[0]["total"] == 64 * 1024 * 1024 and 0 < f[1]["free"] <= f[1]["total"], [(x["path"], x["mounted"], x["total"], x["free"]) for x in f[:2]])
    check("a DISCONNECTED disk: mounted is false and total and free are 0 (it is shown greyed out, never as an empty disk)", f[2]["mounted"] is False and f[2]["total"] == 0 and f[2]["free"] == 0, f[2])
    check("the internal disk has real numbers", f[3]["mounted"] and f[3]["total"] > 0 and 0 < f[3]["free"] <= f[3]["total"], f[3])
    r2 = jrpc("ps5_nothing")
    check("any other unknown method is still refused (nothing else was opened)", "error" in r2 and r2["error"].get("code") == -32601, r2)
    api("POST", "/api/folders", {"action": "remove", "path": "/mnt/ext1/torrents"}); api("POST", "/api/folders", {"action": "remove", "path": "/mnt/usb2/torrents"}); api("POST", "/api/folders", {"action": "remove", "path": "/mnt/usb3/torrents"}); api("POST", "/api/folders", {"action": "remove", "path": "/data/g37"})
    check("with an EMPTY list the method returns an empty list (the dialog then explains where to add folders)", jrpc("ps5_folders").get("result", {}).get("folders") == [], jrpc("ps5_folders"))
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    sh("umount /mnt/usb2 /mnt/usb3 /mnt/ext1 2>/dev/null; rm -rf /mnt/usb2 /mnt/usb3 /mnt/ext1 /data/g37")
sys.exit(summary())
