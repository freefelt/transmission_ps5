import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from harness import *
def journal(): c, t = api("GET", "/api/notifications"); return json.loads(t)["items"] if c == 200 else []
def sstate(): return json.loads(api("GET", "/api/state")[1])
fresh(); loader = Loader(); m = start_manager(); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(6)
add_torrent("A_active.bin", False); time.sleep(2); wait(lambda: sstate()["autosleep"]["can_arm"], 25)
print("== the auto-sleep time limit is an ordinary saved setting; the activation is not")
check("default time limit 240 min is in the settings", sstate()["config"]["autosleep_timeout_minutes"] == 240)
check("out-of-range limits are refused (0 and 1441)", api("POST", "/api/config", {"autosleep_timeout_minutes": 0})[0] == 400 and api("POST", "/api/config", {"autosleep_timeout_minutes": 1441})[0] == 400)
c, t = api("POST", "/api/config", {"autosleep_timeout_minutes": 90, "sleep_countdown_seconds": 4, "sleep_notify_before": False}); cfg = open("/data/Transmission/config.cfg").read()
check("the limit, the delay and the notification flag are saved together", c == 200 and "autosleep_timeout_minutes=90" in cfg and "sleep_countdown_seconds=4" in cfg and "sleep_notify_before=no" in cfg)
before = sstate()["autosleep"]["armed"]
c, t = api("POST", "/api/autosleep", {"action": "arm"}); a = sstate()["autosleep"]
check("'Активировать' needs no parameters: it takes the saved limit (90 min)", c == 200 and a["armed"] and a["timeout_minutes"] == 90 and 5300 < a["timeout_remaining"] <= 5400, a)
c, t = api("POST", "/api/config", {"autosleep_timeout_minutes": 30}); a = sstate()["autosleep"]
check("saving a new limit while activated restarts the countdown from now (30 min)", c == 200 and a["timeout_minutes"] == 30 and 1700 < a["timeout_remaining"] <= 1800, a)
api("POST", "/api/autosleep", {"action": "disarm"})
cfg = open("/data/Transmission/config.cfg").read()
check("the activation itself is not in config.cfg", "armed" not in cfg and "autosleep_armed" not in cfg and sstate()["autosleep"]["armed"] is False)
ev = [i for i in journal() if i.get("kind") == 2]
txt = " | ".join(i["text"] for i in ev)
check("the bell journal got the events: activated, limit changed, deactivated", "auto-sleep activated" in txt and "time limit changed to 30 min" in txt and "auto-sleep deactivated" in txt, txt[:200])
check("they are of kind 'event' (2): neither 'sent' nor 'disabled'", all(i["shown"] is False for i in ev) and len(ev) >= 3)
check("regular notifications keep their kinds (1 = sent)", any(i.get("kind") == 1 for i in journal()))
kill_all(); loader.close()
sys.exit(summary())
