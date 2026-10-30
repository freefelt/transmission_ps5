import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json
from harness import *
import harness; harness.AUTO_FOLDER = False   # we write the config ourselves
CFG = "/data/Transmission/config.cfg"
def full_state():
    c, t = api("GET", "/api/state"); return json.loads(t)
def start_with_cfg(text):
    global loader
    if loader is not None: loader.close()
    fresh(); loader = Loader(); os.makedirs("/data/Transmission", exist_ok=True)
    if text is not None: open(CFG, "w", encoding="utf-8").write(text)
    m = start_manager(); wait(lambda: dstate() is not None, 40); time.sleep(2)
    return full_state()["config"], open(CFG, encoding="utf-8").read()
loader = None
try:
    print("== A) the waiting time before a forced restart: 120 s by default (was 15 s); old configs are migrated ONCE")
    cfg, txt = start_with_cfg(None)
    check("a NEW installation: 120 s, the marker is written to config.cfg", cfg["daemon_restart_after_seconds"] == 120 and "daemon_restart_default_migrated=yes" in txt, (cfg["daemon_restart_after_seconds"], "daemon_restart_default_migrated=yes" in txt))
    cfg, txt = start_with_cfg("daemon_restart_after_seconds=15\n")
    check("an old config that holds the previous default (15) and has no marker becomes 120", cfg["daemon_restart_after_seconds"] == 120 and "daemon_restart_after_seconds=120" in txt, cfg["daemon_restart_after_seconds"])
    cfg, txt = start_with_cfg("daemon_restart_after_seconds=15\ndaemon_restart_default_migrated=yes\n")
    check("15 chosen deliberately (the marker is already there) stays 15", cfg["daemon_restart_after_seconds"] == 15, cfg["daemon_restart_after_seconds"])
    cfg, txt = start_with_cfg("daemon_restart_after_seconds=30\n")
    check("another value (30) is never touched, the marker is added", cfg["daemon_restart_after_seconds"] == 30 and "daemon_restart_default_migrated=yes" in txt, cfg["daemon_restart_after_seconds"])
    print("== B) the page knows the daemon is busy: how long, and when the forced restart comes")
    cfg, txt = start_with_cfg(None)
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    st = full_state()
    check("a healthy daemon: no 'busy' data (unresponsive_for and restart_in are -1, stuck is false)", st["daemon"]["unresponsive_for"] == -1 and st["daemon"]["restart_in"] == -1 and st["daemon"]["stuck"] is False, st["daemon"])
    check("the stats carry the total speeds (down_bps, up_bps; numbers)", isinstance(st["stats"]["down_bps"], int) and isinstance(st["stats"]["up_bps"], int) and st["stats"]["down_bps"] >= 0, (st["stats"].get("down_bps"), st["stats"].get("up_bps")))
    api("POST", "/api/config", {"daemon_restart_after_seconds": 70, "daemon_stop_grace_seconds": 5})
    pid0 = daemon_pids()[0]; os.kill(pid0, signal.SIGSTOP)
    ok = wait(lambda: dstate()["state"] == "not_responding", 60)
    d1 = full_state()["daemon"]; time.sleep(6); d2 = full_state()["daemon"]
    check("a frozen daemon: state 'not_responding', it has been silent >= 10 s and the forced restart is announced in at most 60 s", ok and d1["unresponsive_for"] >= 10 and 0 <= d1["restart_in"] <= 60 and d1["stuck"] is False, d1)
    check("the countdown runs: 6 s later the restart comes about 6 s sooner, the silence is 6 s longer", d1["restart_in"] - d2["restart_in"] in (5, 6, 7) and d2["unresponsive_for"] - d1["unresponsive_for"] in (5, 6, 7), (d1["restart_in"], d2["restart_in"], d1["unresponsive_for"], d2["unresponsive_for"]))
    check("it was NOT restarted before the time was up (same pid, one launch)", daemon_pids() == [pid0] and loader.count == 1, (daemon_pids(), loader.count))
    os.kill(pid0, signal.SIGCONT)
    ok = wait(lambda: dstate()["state"] == "running" and rpc_ok(), 40); d3 = full_state()["daemon"]
    check("the daemon answers again before the deadline: no restart, the busy data are gone", ok and daemon_pids() == [pid0] and d3["unresponsive_for"] == -1 and d3["restart_in"] == -1, d3)
finally:
    try:
        for p in daemon_pids(): os.kill(p, signal.SIGCONT)
    except Exception: pass
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
