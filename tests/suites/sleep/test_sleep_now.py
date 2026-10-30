import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from sleep_helpers import *
loader, m, ha, hc = setup()
print("== S1) the diagnostics button 'Go to sleep now': immediate, Transmission stopped like by the button, then rest mode")
pid0 = daemon_pids()[0]; before = suspends(); seen_countdown = False
c, t = api("POST", "/api/sleep", {"action": "start"})
for _ in range(120):
    if sstate()["sleep"]["phase"] == "countdown": seen_countdown = True
    if suspends() > before: break
    time.sleep(0.5)
check("accepted; there is NO countdown for the manual test button", c == 200 and not seen_countdown, f"countdown seen={seen_countdown}")
check("the rest-mode call was made after the daemon was stopped", suspends() == before + 1 and not daemon_pids(), f"suspends={suspends()}, daemon={daemon_pids()}")
lines = open("/data/Transmission/paused_by_stop.txt").read().split() if os.path.exists("/data/Transmission/paused_by_stop.txt") else []
check("the stop was the 'button' kind: active torrent remembered for resume (R), user-paused kept (P)", f"R{ha}" in lines and f"P{hc}" in lines, lines)
check("Transmission was closed gracefully before sleep", "Closing transmission session" in open("/data/Transmission/logs/console.log").read() and "Transmission stopped before sleep." in mgrlog())
sl = sstate(); check("the manager waits for the wake-up: holding, desired state is still 'running'", sl["sleep"]["holding"] is True and sl["daemon"]["desired_running"] is True, sl["sleep"])
time.sleep(25); check("the daemon is NOT started again while the console is 'asleep'", not daemon_pids() and loader.count == 1, f"launches={loader.count}")
print(">>> the console wakes up (SIGCONT)")
wake(); ok = wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 60)
check("after the wake-up Transmission starts again by itself", ok and loader.count == 2 and "wake-up detected" in shim(), f"launches={loader.count}")
c, t = api("GET", "/api/notifications"); evs = [i["text"] for i in json.loads(t)["items"] if i.get("kind") == 2]
check("the bell journal has the sleep events: going to sleep and wake-up", any("going to sleep" in x for x in evs) and any("wake-up detected" in x for x in evs), evs[-3:])
c, t = api("GET", "/api/diag/sleeplog"); check("'События сна' shows the sleep request, the stop and the wake-up from the log file", c == 200 and "sleep: requesting rest mode" in t and "wake-up detected" in t and "Transmission stopped before sleep" in t, t.count("\n"))
time.sleep(4); st = torrents(); check("the torrent that was active resumes, the user-paused one stays paused", st["A_active.bin"] != 0 and st["C_paused_by_user.bin"] == 0, st)
check("a second request while one is running is refused", (lambda: (api("POST", "/api/sleep", {"action": "start"}), api("POST", "/api/sleep", {"action": "start"})[0])[1] == 409)())
wake(); time.sleep(5)
c, t = api("POST", "/api/config", {"sleep_notify_before": False, "sleep_countdown_seconds": 8}); cfg = open("/data/Transmission/config.cfg").read()
check("the two auto-sleep settings are saved to config.cfg (notification + delay)", c == 200 and "sleep_notify_before=no" in cfg and "sleep_countdown_seconds=8" in cfg)
kill_all(); loader.close()
sys.exit(summary())
