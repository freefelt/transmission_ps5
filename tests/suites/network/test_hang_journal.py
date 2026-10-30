import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, signal, time, os
from harness import *
import harness; harness.AUTO_FOLDER = False
def bell(): c, t = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(t)["items"]] if c == 200 else []
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(4)
    print("== a hang that ENDS BY ITSELF (the daemon is frozen for ~35 s, then continues)")
    api("POST", "/api/config", {"daemon_restart_after_seconds": 300, "daemon_stop_grace_seconds": 5})
    pid = daemon_pids()[0]; os.kill(pid, signal.SIGSTOP); t0 = time.time(); time.sleep(35); os.kill(pid, signal.SIGCONT)
    got = wait(lambda: any("Transmission снова отвечает: не отвечал" in x for x in bell()), 30)
    txt = [x for x in bell() if "Transmission снова отвечает" in x]
    import re
    secs = int(re.search(r"не отвечал (?:(\d+) мин )?(\d+) с", txt[0]).group(2)) + 60 * int(re.search(r"не отвечал (?:(\d+) мин )?(\d+) с", txt[0]).group(1) or 0) if txt else -1
    check("the bell records it with the duration: 'Transmission снова отвечает: не отвечал NN с' (about the 35 s the daemon was frozen)", got and 25 <= secs <= 45, txt)
    print("== a hang that ends with a FORCED RESTART")
    api("POST", "/api/config", {"daemon_restart_after_seconds": 12, "daemon_stop_grace_seconds": 5})
    pid = daemon_pids()[0]; os.kill(pid, signal.SIGSTOP)
    ok = wait(lambda: daemon_pids() and daemon_pids()[0] != pid and rpc_ok() and dstate()["state"] == "running", 90); time.sleep(3)
    txt = [x for x in bell() if "перезапущен после зависания" in x]
    check("the manager restarted the frozen daemon and the bell says so with the duration: 'Transmission перезапущен после зависания: не отвечал NN с'", ok and len(txt) == 1 and re.search(r"не отвечал (\d+ мин )?\d+ с", txt[0]) is not None, txt)
    check("there is exactly ONE entry about this episode: no second 'снова отвечает' / 'перезапущен: простой' for the same hang", sum(1 for x in bell() if "Transmission снова отвечает" in x) == 1 and sum(1 for x in bell() if "перезапущен: простой" in x) == 0, [x[:70] for x in bell()][-6:])
    check("the manager page shows no stale 'busy' data after the restart (state running, no countdown)", (lambda d: d["state"] == "running" and d["unresponsive_for"] == -1 and d["restart_in"] == -1)(json.loads(api("GET", "/api/state")[1])["daemon"]), None)
finally:
    try:
        for p in daemon_pids(): os.kill(p, signal.SIGCONT)
    except Exception: pass
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
