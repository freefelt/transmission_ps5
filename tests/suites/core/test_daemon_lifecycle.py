import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from harness import *
fresh(); loader = Loader(); m = start_manager()
print("== 1) first start: the manager unpacks and launches the daemon")
ok = wait(lambda: daemon_pids() and rpc_ok(), 70)
blob = os.path.getsize(f"{OUT}/transmission_d_host")
check("daemon is running and answers RPC after the manager starts", ok, f"daemon pids {daemon_pids()}")
check("the embedded daemon was written to /data/Transmission/transmission/transmission_d.elf (same size as the blob)", os.path.exists("/data/Transmission/transmission/transmission_d.elf") and os.path.getsize("/data/Transmission/transmission/transmission_d.elf") == blob, blob)
check("exactly one launch through the loader; the daemon is a separate process from the manager", loader.count == 1 and daemon_pids() and set(daemon_pids()).isdisjoint(mgr_pids()), f"launches={loader.count}")
wait(lambda: dstate()["state"] == "running" and daemon_pids() and dstate()["pid"] == daemon_pids()[0], 20)   # the manager looks at the daemon once a second: on a slow machine the state follows the process a moment later
d = dstate(); check("settings page reports the daemon as running with its pid", d and d["state"] == "running" and d["pid"] == daemon_pids()[0], d)
check("the manager tells about the start (notification)", wait(lambda: "Transmission started." in mgrlog(), 15), "")

ida, ha = add_torrent("A_active.bin", False); idb, hb = add_torrent("B_active.bin", False); idc, hc = add_torrent("C_paused_by_user.bin", True); time.sleep(2)
st = torrents(); check("test torrents: A and B active, C paused by the user", st["A_active.bin"] != 0 and st["B_active.bin"] != 0 and st["C_paused_by_user.bin"] == 0, st)
pid1 = daemon_pids()[0]

print("== 2) 'Stop': graceful signal, active torrents paused and remembered")
c, t = api("POST", "/api/daemon", {"action": "stop"}); check("stop request accepted", c == 200 and json.loads(t)["ok"], t)
check("state goes through 'stopping' to 'stopped'", wait(lambda: dstate()["state"] == "stopped", 45), dstate())
check("the daemon process is gone", not daemon_pids())
lines = open("/data/Transmission/paused_by_stop.txt").read().split() if os.path.exists("/data/Transmission/paused_by_stop.txt") else None
check("remembered: A and B as 'to resume' (R), the user-paused C as 'keep paused' (P)", lines is not None and {l[1:] for l in lines if l[0] == "R"} == {ha, hb} and {l[1:] for l in lines if l[0] == "P"} == {hc}, lines)
check("the daemon received a termination signal and closed its session properly", "Closing transmission session" in open("/data/Transmission/logs/console.log").read())
check("desired state saved as 'stopped' (the daemon will not come back by itself)", open("/data/Transmission/daemon.desired").read().strip() == "stopped" and dstate()["desired_running"] is False)
time.sleep(25); check("and it really does not come back during the next 25 s", not daemon_pids() and loader.count == 1, f"launches={loader.count}")
check("the manager tells about the stop with the number of paused torrents", "Torrents paused: 2" in mgrlog(), "")

import glob
lost = [f for f in glob.glob("/data/Transmission/transmission/config/resume/*") if hc[:16] in f]
for f in lost: os.remove(f)
print(f"(simulating lost state: removed {len(lost)} resume file of the user-paused torrent C, as if the shutdown had been cut short)")
print("== 3) 'Start': the daemon returns and ONLY the remembered torrents resume")
c, t = api("POST", "/api/daemon", {"action": "start"}); check("start request accepted", c == 200 and json.loads(t)["ok"], t)
check("daemon running again", wait(lambda: dstate()["state"] == "running" and rpc_ok(), 70), dstate())
time.sleep(3); st = torrents()
check("A and B were resumed, C (paused by the user) stays paused", st["A_active.bin"] != 0 and st["B_active.bin"] != 0 and st["C_paused_by_user.bin"] == 0, st)
check("Transmission started C by itself (no state file) and the manager returned it to the paused state", "returning 1 torrent(s) to the paused state" in shim(), "")
check("the remembered list is consumed (deleted)", not os.path.exists("/data/Transmission/paused_by_stop.txt"))
check("new process, second launch", daemon_pids() and daemon_pids()[0] != pid1 and loader.count == 2, f"launches={loader.count}")
check("the notification mentions the resumed torrents", "Resumed torrents: 2" in mgrlog(), "")
check("starting a running daemon is refused politely", json.loads(api("POST", "/api/daemon", {"action": "start"})[1])["ok"] is False)
kill_all(); loader.close()
sys.exit(summary())
