import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from harness import *
def journal(): c, t = api("GET", "/api/notifications"); return json.loads(t)["items"] if c == 200 else []
os.makedirs("/data/torrents", exist_ok=True)
fresh(); loader = Loader(); m = start_manager(); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(6)
print("== 1) settings: validation and storage")
st = json.loads(api("GET", "/api/state")[1])
check("defaults are 5 GB and 1 GB; the page gets the embedded Transmission version", st["config"]["low_disk_warn_gb"] == 5 and st["config"]["low_disk_critical_gb"] == 1 and st["tr_version"] == "4.1.4", (st["config"]["low_disk_warn_gb"], st["config"]["low_disk_critical_gb"], st["tr_version"]))
c, t = api("POST", "/api/config", {"low_disk_warn_gb": 20, "low_disk_critical_gb": 3}); cfg = open("/data/Transmission/config.cfg").read()
check("both thresholds are saved to config.cfg", c == 200 and "low_disk_warn_gb=20" in cfg and "low_disk_critical_gb=3" in cfg, t[:80])
c, t = api("POST", "/api/config", {"low_disk_critical_gb": 30}); st = json.loads(api("GET", "/api/state")[1])
check("the 'almost full' threshold above the 'low' one is refused and nothing changes", c == 400 and "должен быть меньше" in t and st["config"]["low_disk_critical_gb"] == 3, t[:120])
c, t = api("POST", "/api/config", {"low_disk_warn_gb": 5, "low_disk_critical_gb": 6}); st = json.loads(api("GET", "/api/state")[1])
check("the same check when both are changed together (and the old values come back)", c == 400 and st["config"]["low_disk_warn_gb"] == 20 and st["config"]["low_disk_critical_gb"] == 3, t[:100])
check("out-of-range values are refused", api("POST", "/api/config", {"low_disk_warn_gb": 0})[0] == 400 and api("POST", "/api/config", {"low_disk_warn_gb": 100001})[0] == 400)
check("0 for the 'almost full' notification is allowed (= do not send)", api("POST", "/api/config", {"low_disk_critical_gb": 0})[0] == 200)

print("== 2) the thresholds really drive the notifications (using the real free space of this machine)")
api("POST", "/api/config", {"low_disk_critical_gb": 1, "low_disk_warn_gb": 100000})
ida, ha = add_torrent("A_active.bin", False)
ok = wait(lambda: any("Low disk space" in i["text"] for i in journal()), 60)
low = [i for i in journal() if "Low disk space" in i["text"]]
check("free space below the 'low' threshold: 'Low disk space' is announced (and sent to the console)", ok and low[0]["shown"] and "Low disk space" in mgrlog(), low[:1])
api("POST", "/api/config", {"low_disk_critical_gb": 99999})
ok = wait(lambda: any("Disk almost full" in i["text"] for i in journal()), 60)
check("free space below the 'almost full' threshold: the second, urgent notification follows", ok, [i["text"][:60] for i in journal() if "Disk almost full" in i["text"]][:1])
kill_all(); loader.close()
sys.exit(summary())
