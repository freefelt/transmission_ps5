import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, time, os
from harness import *
# The console does not freeze the payload in rest mode, so the power state of the system (the event flag SceSystemStateMgrInfo; imitated here by /tmp/ps5tm_power_state) tells it:
# the daemon is stopped cleanly before sleep and shutdown, and started again when the state is "working" again.
PS, ST = "/tmp/ps5tm_power_state", "/tmp/ps5tm_power_status"
def setpw(n): open(PS, "w").write(f"{n}\n")
def clear():
    for f in (PS, ST):
        if os.path.exists(f): os.remove(f)
def power(): return json.loads(api("GET", "/api/state")[1])["stats"]["power"]
def desired(): return open("/data/Transmission/daemon.desired").read().strip() if os.path.exists("/data/Transmission/daemon.desired") else None
clear(); fresh(); loader = Loader(); m = start_manager()
try:
    ok = wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70)
    api("POST", "/api/config", {"wake_delay_seconds": 3})
    check("the monitor of the power state is ready and the console is 'working' (1000)", wait(lambda: power().get("ready") is True, 15) and power()["state"] == 1000 and power()["name"] == "working" and not power()["disabled"], power())
    ida, ha = add_torrent("A_active.bin", False); idb, hb = add_torrent("B_active.bin", False); idc, hc = add_torrent("C_paused_by_user.bin", True); time.sleep(2)
    check("setup: two active torrents, one paused by the user", torrents()["A_active.bin"] != 0 and torrents()["B_active.bin"] != 0 and torrents()["C_paused_by_user.bin"] == 0, torrents())

    print("== 1) the console goes to sleep (suspend on going)")
    setpw(300)
    check("the daemon is stopped cleanly (it does not wait for the sleep)", wait(lambda: dstate()["state"] == "stopped" and not daemon_pids(), 40), dstate())
    lines = open("/data/Transmission/paused_by_stop.txt").read().split() if os.path.exists("/data/Transmission/paused_by_stop.txt") else None
    check("the active torrents are remembered to resume (R), the user's one stays paused (P)", lines is not None and {l[1:] for l in lines if l[0] == "R"} == {ha, hb} and {l[1:] for l in lines if l[0] == "P"} == {hc}, lines)
    check("the manager says so: 'Transmission stopped before sleep'", wait(lambda: "Transmission stopped before sleep" in mgrlog(), 10), "")
    check("this is not a stop by the user: the desired state stays 'running' (the file exists only for 'stopped')", desired() in (None, "running"), desired())
    check("the state is shown: suspend, sleeping", power()["name"] == "suspend" and power()["sleeping"] is True, power())
    time.sleep(12); check("the daemon is NOT started again while the console sleeps (the network is still there, the state says sleep)", not daemon_pids() and dstate()["state"] == "stopped", dstate())
    setpw(500); time.sleep(6)
    check("main on standby: still stopped", not daemon_pids() and power()["name"] == "standby", power())

    print("== 2) the console wakes up (working)")
    setpw(1000)
    check("the daemon starts again by itself", wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 60), dstate())
    time.sleep(3); st = torrents()
    check("the remembered torrents are resumed, the user's one stays paused", st["A_active.bin"] != 0 and st["B_active.bin"] != 0 and st["C_paused_by_user.bin"] == 0, st)
    check("the log and the bell say that the console is awake", "the console is awake" in shim() and not power()["sleeping"], "")

    print("== 3) the daemon that the user has stopped stays stopped")
    api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped", 45)
    setpw(300); time.sleep(4); setpw(1000); time.sleep(10)
    check("sleep and wake-up change nothing: still stopped, and the desired state is still 'stopped'", not daemon_pids() and desired() == "stopped", (daemon_pids(), desired()))
    api("POST", "/api/daemon", {"action": "start"}); check("(started again by the user)", wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 60), dstate())

    print("== 4) the shell UI is shutting down (a bit of SceSystemStateMgrStatus) is a sleep too")
    open(ST, "w").write("200000\n")
    check("the daemon is stopped", wait(lambda: not daemon_pids() and dstate()["state"] == "stopped", 40), dstate())
    os.remove(ST); time.sleep(4)
    check("the bit alone going away is not 'awake' (the state may still be sleeping)... but with the state 'working' the daemon comes back", wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 60), dstate())

    print("== 5) shutdown (100): the daemon is stopped and never started again")
    setpw(100)
    check("the daemon is stopped", wait(lambda: not daemon_pids() and dstate()["state"] == "stopped", 40), dstate())
    time.sleep(12); check("and it stays stopped while the state is 'shutdown'", not daemon_pids(), daemon_pids())
    setpw(1000); wait(lambda: daemon_pids() and rpc_ok(), 60)

    print("== 6) a previous start that did not return: the monitor stays off")
    kill_all(); loader.close(); clear()
    os.makedirs("/data/Transmission", exist_ok=True); open("/data/Transmission/power_probe.txt", "w").write("open\n")
    loader = Loader(); m = start_manager()
    check("the probe mark of a crashed start is found: the monitor is off and writes the skip file", wait(lambda: power().get("disabled") is True, 40) and os.path.exists("/data/Transmission/power_skip.txt"), power())
    check("the daemon works as before (nothing stops it)", wait(lambda: daemon_pids() and rpc_ok(), 70), "")
finally:
    clear()
    for f in ("/data/Transmission/power_probe.txt", "/data/Transmission/power_skip.txt"):
        if os.path.exists(f): os.remove(f)
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
