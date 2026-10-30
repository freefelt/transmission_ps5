import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, time, os, signal, base64, hashlib, subprocess
from harness import *
import harness; harness.SYS_HELPER = True
OFFMARK = "/tmp/ps5tm_test_poweroff"
def bell(): c, t = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(t)["items"]] if c == 200 else []
def meta(name): return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def astate(): return json.loads(api("GET", "/api/state")[1])
for f in (OFFMARK, "/data/Transmission/paused_by_stop.txt"):
    if os.path.exists(f): os.remove(f)
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    s = astate()
    check("the setting 'Действие' (autosleep_action) exists, default 'sleep'; the sleep info has a 'manual' flag", s["config"]["autosleep_action"] == "sleep" and s["sleep"]["manual"] is False and "autosleep_action=sleep" in open(CFG := "/data/Transmission/config.cfg").read(), s["config"].get("autosleep_action"))
    c, _ = api("POST", "/api/config", {"autosleep_action": "nautilus"}); check("a wrong value is refused", c != 200 and astate()["config"]["autosleep_action"] == "sleep", c)
    rpc("torrent-add", metainfo=meta("po_a"), paused=False); time.sleep(2)
    print("== a) the console went to sleep by itself while auto-sleep is armed: waking up deactivates it")
    api("POST", "/api/config", {"autosleep_timeout_minutes": 30, "sleep_notify_before": False})
    c, t = api("POST", "/api/autosleep", {"action": "arm"}); time.sleep(1)
    check("auto-sleep is armed", astate()["autosleep"]["armed"] is True, t[:80])
    os.kill(m.pid, signal.SIGCONT); ok = wait(lambda: astate()["autosleep"]["armed"] is False, 12)
    check("a wake-up (SIGCONT, as after a sleep) DEACTIVATES auto-sleep, and the bell says so", ok and any("auto-sleep deactivated: the console woke up" in x for x in bell()), [x[:80] for x in bell()][-2:])
    time.sleep(3)
    check("and the console is NOT put to sleep because of the time that passed", not os.path.exists(OFFMARK) and astate()["sleep"]["phase"] == "idle", astate()["sleep"])
    print("== b) the action 'Выключить консоль': at the end the console is powered off after a graceful stop")
    api("POST", "/api/config", {"autosleep_action": "poweroff", "autosleep_timeout_minutes": 1, "sleep_notify_before": False})
    c, t = api("POST", "/api/autosleep", {"action": "arm"}); time.sleep(1)
    seen = wait(lambda: any("auto-sleep activated: power off when all downloads finish" in x for x in bell()), 8)
    check("armed with the action 'poweroff' (the journal says 'power off')", astate()["autosleep"]["armed"] is True and seen, (astate()["autosleep"], [x[:90] for x in bell()][-1:]))
    ok = wait(lambda: os.path.exists(OFFMARK), 120)
    check("after the time limit (1 min) the power off call was made (here: the marker file instead of the real call)", ok, None)
    pause_list = open("/data/Transmission/paused_by_stop.txt").read() if os.path.exists("/data/Transmission/paused_by_stop.txt") else ""
    check("before it Transmission was stopped gracefully: the active torrent was paused and remembered to be resumed at the next start", pause_list.startswith("R"), pause_list[:20])
    check("the bell says 'Power off: turning the console off', not 'going to sleep'", any("Power off: turning the console off" in x for x in bell()) and not any("Sleep: going to sleep" in x for x in bell()), [x[:90] for x in bell()][-3:])
    ok = wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 90)
    check("the call returned (it cannot on a real console): the manager does not leave Transmission stopped, it starts the daemon again", ok, dstate()["state"])
    print("== c) full quit: confirmation is in the page, here the request itself")
    hp = wait(lambda: json.loads(api("GET", "/api/state")[1])["stats"]["syssvc"]["ready"] is True, 40)
    check("(the system service runs inside the manager)", hp, None)
    c, t = api("POST", "/api/quit", {})
    check("POST /api/quit answers at once with ok", c == 200 and json.loads(t)["ok"] is True, t)
    ok = wait(lambda: m.poll() is not None, 90)
    check("the manager process EXITS by itself (no kill from outside)", ok and m.poll() == 0, m.poll())
    check("Transmission (the daemon) is gone too, shut down gracefully", not daemon_pids(), daemon_pids())
    check("the torrents that were running stay in the list of paused ones: they resume at the next start", os.path.exists("/data/Transmission/paused_by_stop.txt"), None)
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
