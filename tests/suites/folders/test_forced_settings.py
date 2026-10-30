import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, subprocess
from harness import *
S = "/data/Transmission/transmission/config/settings.json"
def jread(): return json.load(open(S, encoding="utf-8"))
def stop_daemon():
    api("POST", "/api/daemon", {"action": "stop"})
    wait(lambda: dstate()["state"] == "stopped" and not dstate()["busy"], 45)   # the manager is free now: otherwise the next "start" request is refused
def start_daemon():
    for _ in range(20):
        c, _t = api("POST", "/api/daemon", {"action": "start"})
        if c == 200: break
        time.sleep(1)   # the manager is still busy with the previous action: retry
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80)
def restart_with(mutate):
    """stop the daemon, change settings.json (mutate gets the dict), start it: the start must fix the settings"""
    stop_daemon()
    d = jread(); mutate(d); json.dump(d, open(S, "w", encoding="utf-8"), indent=4, ensure_ascii=False)
    start_daemon(); time.sleep(2)
    return jread()
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    d = jread()
    check("a NEW installation: preallocation is OFF (0) and the temporary folder is off in the default settings.json", d.get("preallocation") == 0 and d.get("incomplete-dir-enabled") is False, (d.get("preallocation"), d.get("incomplete-dir-enabled")))
    d = restart_with(lambda x: x.update({"preallocation": 1, "incomplete-dir-enabled": True, "peer-port": 51499}))
    check("an EXISTING installation with the old default (preallocation 1 = 'fast/sparse') and the temporary folder ON: both are fixed at the daemon start, other settings stay", d.get("preallocation") == 0 and d.get("incomplete-dir-enabled") is False and d.get("peer-port") == 51499, (d.get("preallocation"), d.get("incomplete-dir-enabled"), d.get("peer-port")))
    d = restart_with(lambda x: x.update({"preallocation": 2}))
    check("the 'full' mode (2) is switched off too", d.get("preallocation") == 0, d.get("preallocation"))
    d = restart_with(lambda x: x.update({"preallocation": "fast"}))
    check("the textual form ('fast') is switched off too (becomes the number 0)", d.get("preallocation") == 0, d.get("preallocation"))
    d = restart_with(lambda x: (x.pop("preallocation", None), x.update({"peer-port": 51500})))
    check("a settings.json WITHOUT the key (Transmission would use its default 'fast') gets preallocation 0 added, the file stays valid JSON, other settings stay", d.get("preallocation") == 0 and d.get("peer-port") == 51500, (d.get("preallocation"), d.get("peer-port")))
    check("the daemon works after all that (RPC answers, session-get works)", rpc_ok() and rpc("session-get", fields=["version"])["result"] == "success", None)
    log = open("/data/Transmission/logs/daemon.log", encoding="utf-8", errors="replace").read()
    check("every fix is written to the daemon log ('preallocation switched OFF')", log.count("preallocation switched OFF") >= 4 and "temporary folder was ON" in log, log.count("preallocation switched OFF"))
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
