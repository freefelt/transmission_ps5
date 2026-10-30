import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, hashlib, base64, subprocess
from harness import *
import web_stack
def sh(c): return subprocess.run(c, shell=True, capture_output=True, text=True)
def tget(name):
    ts = [t for t in rpc("torrent-get", fields=["name", "id", "status", "error", "errorString", "percentDone", "downloadDir"])["arguments"]["torrents"] if t["name"] == name]; return ts[0] if ts else None
def tid(name): return tget(name)["id"]
def start(name): return rpc("torrent-start", ids=[tid(name)])
def verify(name): return rpc("torrent-verify", ids=[tid(name)])
def stop(name): return rpc("torrent-stop", ids=[tid(name)])
def status(name): return tget(name)["status"]      # 0 stopped, 2 check, 3 download queue, 4 downloading, 5 seeding queue, 6 seeding
def plug(): sh("mount --bind /tmp/g25_store /mnt/usb2")
def unplug(): sh("umount /mnt/usb2")
def jtexts():
    c, t = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(t)["items"]] if c == 200 else []
def meta(name):
    return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def cleanup():
    sh("umount /mnt/usb2 2>/dev/null; umount /tmp/g25_store 2>/dev/null; rm -rf /mnt/usb2 /tmp/g25_store /data/torrents")
cleanup(); shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
os.makedirs("/mnt/usb2", exist_ok=True); os.makedirs("/tmp/g25_store", exist_ok=True)
sh("mount -t tmpfs -o size=64m tmpfs /tmp/g25_store"); plug()
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    api("POST", "/api/folders", {"action": "add", "path": "/mnt/usb2/torrents", "confirm": True})
    print("== A) the disk is connected: everything works as before")
    web_stack.add_complete_in("T1.bin", "/mnt/usb2/torrents")           # a torrent WITH DATA on the disk
    web_stack.add_complete_in("T2.bin", "/data/torrents")               # a torrent on the internal disk (outside the external disk rules)
    rpc("torrent-add", metainfo=meta("T0.bin"), paused=True, **{"download-dir": "/mnt/usb2/torrents"})   # a torrent WITHOUT data (0%)
    ok_seed = wait(lambda: tget("T1.bin") and tget("T1.bin")["percentDone"] == 1.0 and status("T1.bin") in (5, 6) and tget("T2.bin") and status("T2.bin") in (5, 6), 40)
    check("with the disk connected: the torrent with data seeds (after the first check), the one on the internal disk too", ok_seed, (tget("T1.bin"), tget("T2.bin")))
    start("T0.bin"); time.sleep(2)
    check("a torrent WITHOUT data starts normally while the disk is connected (downloading, no error)", status("T0.bin") in (3, 4) and tget("T0.bin")["error"] == 0, tget("T0.bin"))
    stop("T0.bin"); time.sleep(1)
    print("== B) the disk is unplugged: start and Verify are refused")
    unplug(); time.sleep(0.5)
    r = start("T0.bin"); time.sleep(2); t0 = tget("T0.bin")
    check("a torrent WITHOUT data (the case Transmission itself lets through) does NOT start: stopped, a local error names the disk", r["result"] == "success" and t0["status"] == 0 and t0["error"] == 3 and "The drive /mnt/usb2 is not connected: the torrent was not started" in t0["errorString"], t0)
    check("nothing was created or written where the unplugged disk should be", os.listdir("/mnt/usb2") == [], os.listdir("/mnt/usb2"))
    verify("T1.bin"); verify("T0.bin"); time.sleep(2); t1 = tget("T1.bin"); t0 = tget("T0.bin")
    check("Verify is refused for torrents of the unplugged disk: the progress is NOT reset (T1 stays 100%), nothing is 'checking'", t1["percentDone"] == 1.0 and t1["status"] != 2 and t0["status"] != 2 and "the check is impossible" in t1["errorString"] and "the check is impossible" in t0["errorString"], (t1["percentDone"], t1["status"], t1["errorString"][:70]))
    stop("T2.bin"); time.sleep(1); start("T2.bin"); time.sleep(2)
    check("a torrent on the INTERNAL disk is not affected: it can be stopped and started while the external disk is unplugged", status("T2.bin") in (5, 6) and tget("T2.bin")["error"] == 0, tget("T2.bin"))
    check("the refusals reach the bell ('Отклонено: Диск /mnt/usb2 не подключён: торрент не запущен' and '…проверка невозможна…')", wait(lambda: any("the torrent was not started" in x for x in jtexts()) and any("the check is impossible" in x for x in jtexts()), 25), [x[:80] for x in jtexts() if "Отклонено" in x][-3:])
    print("== C) a daemon restart while the disk is unplugged (torrents that were running are resumed at load)")
    plug(); time.sleep(0.5); start("T0.bin"); time.sleep(2); started_before = status("T0.bin") in (3, 4); unplug(); time.sleep(0.5)
    api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped", 45)
    api("POST", "/api/daemon", {"action": "start"}); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80); time.sleep(6)
    t0, t1 = tget("T0.bin"), tget("T1.bin")
    check("after the restart the torrents of the missing disk do NOT start by themselves (loaded as stopped with the disk error), nothing written there", started_before and t0["status"] == 0 and t1["status"] == 0 and "is not connected" in t0["errorString"] + t1["errorString"] and os.listdir("/mnt/usb2") == [], (started_before, t0["status"], t1["status"], t0["errorString"][:60]))
    print("== D) the queue manager must not start them either")
    rpc("session-set", **{"download-queue-enabled": True, "download-queue-size": 1})
    web_stack.add_complete_in("QA.bin", "/data/torrents")
    rpc("torrent-add", metainfo=meta("QB.bin"), paused=True, **{"download-dir": "/data/torrents"}); start("QB.bin"); time.sleep(2)   # QB takes the only slot
    start("T0.bin"); time.sleep(2); queued = status("T0.bin")
    stop("QB.bin"); time.sleep(4); t0 = tget("T0.bin")
    check("a torrent waiting in the queue for an unplugged disk is refused when its turn comes (stopped with the error, not endlessly re-queued)", queued in (0, 3) and t0["status"] == 0 and t0["error"] == 3 and "is not connected" in t0["errorString"] and os.listdir("/mnt/usb2") == [], (queued, t0["status"], t0["errorString"][:60]))
    rpc("session-set", **{"download-queue-enabled": False})
    print("== E) the disk is back: everything resumes")
    plug(); time.sleep(0.5)
    start("T1.bin"); start("T0.bin"); ok_back = wait(lambda: status("T1.bin") in (5, 6) and tget("T1.bin")["percentDone"] == 1.0 and status("T0.bin") in (3, 4), 40)
    check("after reconnecting, Start works again: the torrent with data seeds with its progress intact (100%), the other downloads, errors are gone", ok_back and tget("T1.bin")["error"] == 0 and tget("T0.bin")["error"] == 0, (tget("T1.bin"), tget("T0.bin")))
    verify("T1.bin"); ok_ver = wait(lambda: status("T1.bin") in (5, 6) and tget("T1.bin")["percentDone"] == 1.0 and tget("T1.bin")["error"] == 0, 40)
    check("Verify works again with the disk connected (finds the data, 100%)", ok_ver, tget("T1.bin"))
    log = open("/data/Transmission/logs/daemon.log", encoding="utf-8", errors="replace").read()
    check("the refusals are in the daemon log", log.count("was not started") >= 2 and "the check is impossible" in log, log.count("не запущен"))
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
    cleanup()
sys.exit(summary())
