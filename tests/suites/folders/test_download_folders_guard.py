import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, hashlib, base64, subprocess
from harness import *
import harness; harness.AUTO_FOLDER = False   # these suites start from an EMPTY folder list
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def meta(name):
    pieces = hashlib.sha1(name.encode()).digest()
    return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": pieces}})).decode()
def add(name, folder=None):
    a = {"metainfo": meta(name), "paused": True}
    if folder is not None: a["download-dir"] = folder
    return rpc("torrent-add", **a)
def ok(r): return r["result"] == "success"
def tdir(name):
    ts = [t for t in rpc("torrent-get", fields=["name", "downloadDir"])["arguments"]["torrents"] if t["name"] == name]; return ts[0]["downloadDir"] if ts else None
def tid(name): return [t["id"] for t in rpc("torrent-get", fields=["name", "id"])["arguments"]["torrents"] if t["name"] == name][0]
def setloc(name, loc): return rpc("torrent-set-location", ids=[tid(name)], location=loc, move=False)
def sget(k): return rpc("session-get", fields=[k])["arguments"].get(k)
def post(**b): c, t = api("POST", "/api/folders", b); return c
def cleanup():
    for d in ("/mnt/usb2", "/mnt/ext1"): sh(f"umount {d} 2>/dev/null; rm -rf {d}")
    sh("rm -rf /data/g24 /data/torrents")   # /data/torrents is left on the host by other suites; the test checks that the refusals create nothing
cleanup(); shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
for d in ("/mnt/usb2", "/mnt/ext1"): os.makedirs(d, exist_ok=True)
sh("mount -t tmpfs -o size=64m tmpfs /mnt/usb2"); sh("mount -t tmpfs -o size=64m tmpfs /mnt/ext1")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    print("== A) no download folders yet: nothing can be added")
    r1 = add("t_empty_1"); r2 = add("t_empty_2", "/mnt/usb2/torrents")
    check("with an EMPTY list torrent-add is refused (with and without a folder) and says what to do", not ok(r1) and not ok(r2) and "There are no download folders" in r1["result"] and "There are no download folders" in r2["result"] and tdir("t_empty_1") is None and tdir("t_empty_2") is None, (r1["result"], r2["result"]))
    r = rpc("session-set", **{"incomplete-dir-enabled": True})
    check("the temporary folder cannot be switched ON (session-set), switching it OFF is fine, and the temp folder path cannot be changed", not ok(r) and "The temporary folder is disabled" in r["result"] and ok(rpc("session-set", **{"incomplete-dir-enabled": False})) and not ok(rpc("session-set", **{"incomplete-dir": "/tmp/x"})), r["result"])
    print("== B) with folders in the list")
    codes = [post(action="add", path="/mnt/usb2/torrents", confirm=True), post(action="add", path="/mnt/ext1/Games"), post(action="add", path="/data/g24", confirm=True)]
    check("three folders added through the manager (usb, M.2, internal)", codes == [200, 200, 200], codes)
    r = add("t_default")
    check("torrent-add WITHOUT a folder goes to the default folder (the first in the list) and the daemon sees the list at once (no restart)", ok(r) and tdir("t_default") == "/mnt/usb2/torrents", (r["result"], tdir("t_default")))
    check("session-get shows the default folder as 'download-dir' (the web UI puts it into 'Download to'), free space can be asked", sget("download-dir") == "/mnt/usb2/torrents" and ok(rpc("session-get", fields=["download-dir-free-space"])), sget("download-dir"))
    r = add("t_ext", "/mnt/ext1/Games")
    check("an explicit folder from the list is accepted", ok(r) and tdir("t_ext") == "/mnt/ext1/Games", (r["result"], tdir("t_ext")))
    r = add("t_case", "/MNT/usb2/Torrents/")
    check("another letter case and a trailing slash are matched to the list entry and its own spelling is used", ok(r) and tdir("t_case") == "/mnt/usb2/torrents", (r["result"], tdir("t_case")))
    bad = ["/mnt/usb2/other", "/data/torrents", "/mnt/usb2/torrents/../x", "/mnt/usb2", "/etc"]
    res = [add(f"t_bad{i}", p) for i, p in enumerate(bad)]
    check("folders NOT in the list (another folder, the old /data/torrents, '..', the disk root, /etc) are refused", all(not ok(r) for r in res) and all(tdir(f"t_bad{i}") is None for i in range(len(bad))) and "is not in the list" in res[0]["result"], [r["result"][:60] for r in res])
    check("nothing was created on the disks by the refused adds", not os.path.exists("/mnt/usb2/other") and not os.path.exists("/mnt/usb2/x") and not os.path.exists("/data/torrents"), os.listdir("/mnt/usb2"))
    r = add("t_rel", "mnt/usb2/torrents")
    check("a relative path is still refused by Transmission itself", not ok(r) and "not absolute" in r["result"], r["result"])
    print("== C) torrent-set-location")
    r = setloc("t_default", "/mnt/ext1/Games")
    check("moving to a folder of the list works", ok(r) and tdir("t_default") == "/mnt/ext1/Games", (r["result"], tdir("t_default")))
    r1, r2 = setloc("t_default", "/mnt/usb2/elsewhere"), setloc("t_default", "/data/torrents")
    check("moving to a folder NOT in the list is refused, the torrent stays", not ok(r1) and not ok(r2) and "is not in the list" in r1["result"] and tdir("t_default") == "/mnt/ext1/Games", (r1["result"][:70], r2["result"][:70]))
    print("== D) session-set download-dir")
    r = rpc("session-set", **{"download-dir": "/mnt/ext1/Games"}); d = sget("download-dir"); add("t_default2")
    check("the default folder can be chosen from the list; new torrents without a folder follow it", ok(r) and d == "/mnt/ext1/Games" and tdir("t_default2") == "/mnt/ext1/Games", (r["result"], d, tdir("t_default2")))
    r = rpc("session-set", **{"download-dir": "/mnt/usb2/zzz"})
    check("a folder outside the list cannot be chosen as the default; the default is unchanged", not ok(r) and sget("download-dir") == "/mnt/ext1/Games", r["result"][:80])
    print("== D2) the default folder changes only by the user's choice")
    first = json.loads(api("GET", "/api/folders")[1])["folders"][0]["path"]
    c, _ = api("POST", "/api/folders", {"action": "add", "path": "/mnt/ext1/NewOne"})
    check("adding a folder does not make it the default: the chosen one stays", c == 200 and sget("download-dir") == "/mnt/ext1/Games", (c, sget("download-dir")))
    api("POST", "/api/folders", {"action": "remove", "path": "/mnt/ext1/NewOne"})
    api("POST", "/api/folders", {"action": "remove", "path": "/mnt/ext1/Games"})
    check("when the chosen default folder leaves the list the default is the first folder of the list", sget("download-dir") == first and first != "/mnt/ext1/Games", (sget("download-dir"), first))
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/ext1/Games"}); r = add("t_after_readd")
    check("adding that folder again does NOT make it the default again (the old choice is forgotten); new torrents without a folder still go to the first one", sget("download-dir") == first and tdir("t_after_readd") == first, (sget("download-dir"), first, tdir("t_after_readd")))
    print("== E) the disk is unplugged")
    ok(rpc("session-set", **{"download-dir": "/mnt/usb2/torrents"})); sh("umount /mnt/usb2"); time.sleep(0.5)
    r1 = add("t_off", "/mnt/usb2/torrents"); r2 = add("t_off_default")
    check("adding to a folder of an UNPLUGGED disk is refused (explicitly and as the default folder), with the disk named", not ok(r1) and not ok(r2) and "The drive /mnt/usb2 is not connected" in r1["result"] and "The drive /mnt/usb2 is not connected" in r2["result"] and tdir("t_off") is None, (r1["result"], r2["result"]))
    check("nothing was created on the folder where the unplugged disk should be", os.listdir("/mnt/usb2") == [], os.listdir("/mnt/usb2"))
    r1, r2 = setloc("t_case", "/mnt/ext1/Games"), setloc("t_ext", "/mnt/usb2/torrents")
    check("moving a torrent that lies on the unplugged disk, or moving to the unplugged disk, is refused", not ok(r1) and not ok(r2) and "the move is impossible" in r1["result"] and "the move is impossible" in r2["result"] and tdir("t_case") == "/mnt/usb2/torrents" and tdir("t_ext") == "/mnt/ext1/Games", (r1["result"], r2["result"]))
    print("== F) existing torrents and logging")
    for p in ("/mnt/usb2/torrents", "/mnt/ext1/Games", "/data/g24"): api("POST", "/api/folders", {"action": "remove", "path": p})
    check("after the folders are deleted from the list the existing torrents stay exactly as they were", tdir("t_case") == "/mnt/usb2/torrents" and tdir("t_ext") == "/mnt/ext1/Games" and tdir("t_default") == "/mnt/ext1/Games", (tdir("t_case"), tdir("t_ext")))
    r = add("t_after_empty")
    check("with the list empty again adding is refused again, session-get still works and the default folder is EMPTY (nothing is shown as a folder)", not ok(r) and "There are no download folders" in r["result"] and sget("download-dir") == "", (r["result"][:60], sget("download-dir")))
    log = open("/data/Transmission/logs/daemon.log", encoding="utf-8", errors="replace").read()
    check("every refusal is written to the daemon log ('guard: rejected: ...') for diagnostics", log.count("guard: rejected:") >= 10 and "The drive /mnt/usb2 is not connected" in log, log.count("guard: rejected:"))
    print("== G) the bell: every refusal also appears in the manager's notification journal")
    def jtexts():
        c, tx = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(tx)["items"]] if c == 200 else []
    got_list = wait(lambda: any(x.startswith("Отклонено: ") and "is not in the list" in x for x in jtexts()), 25)
    got_disk = wait(lambda: any(x.startswith("Отклонено: The drive /mnt/usb2 is not connected") for x in jtexts()), 25)
    check("refusals reach the bell: 'Отклонено: Папка ... не входит в список' and 'Отклонено: Диск /mnt/usb2 не подключён ...'", got_list and got_disk, (got_list, got_disk, len(jtexts()), [x[:70] for x in jtexts()][-6:]))
    print("== H) no flood: the same refusal is written to the bell once a minute")
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/ext1/Games"})   # the list is not empty: the refusal names the folder (the text is unique for this check)
    for _ in range(3): add("t_dup", "/mnt/ext1/dupcheck")
    time.sleep(4)
    check("the same refusal is written to the bell only once (not three times) within a minute", sum(1 for x in jtexts() if "/mnt/ext1/dupcheck" in x) == 1, sum(1 for x in jtexts() if "/mnt/ext1/dupcheck" in x))
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    cleanup()
sys.exit(summary())
