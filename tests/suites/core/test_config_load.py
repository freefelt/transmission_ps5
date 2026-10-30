import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from harness import *
import harness; harness.AUTO_FOLDER = False   # checks how the manager creates and parses config.cfg
CALLS = "/tmp/ps5tm_host_calls.log"
def post_raw(path, body, ctype):
    r = urllib.request.Request(f"http://127.0.0.1:9092{path}", data=body.encode(), method="POST"); r.add_header("Content-Type", ctype)
    try: return urllib.request.urlopen(r, timeout=10).status
    except urllib.error.HTTPError as e: return e.code
fresh()
if os.path.exists(CALLS): os.remove(CALLS)
print("== settings: an OLD config.cfg (previous 'all in one' version) must not break the new manager")
os.makedirs("/data/Transmission"); open("/data/Transmission/config.cfg", "w").write("restart_elf_path=/data/Transmission/Transmission.elf\nauto_restart_after_seconds=5\npoll_interval_seconds=7\nnotify_low_disk_space=no\nnotify_web_ui=yes\n")
loader = Loader(); m = start_manager(); wait(lambda: daemon_pids() and rpc_ok(), 70); time.sleep(3)
st = json.loads(api("GET", "/api/state")[1])
check("the manager started with the old file: known settings applied (poll 7 s, low-disk off)", st["config"]["poll_interval_seconds"] == 7 and st["config"]["notify_low_disk_space"] is False)
check("the removed settings were ignored silently (no warnings, no crash)", "restart_elf_path" not in shim() and "auto_restart" not in shim() and "notify_web_ui" not in shim() and mgr_pids(), "")
check("new settings have their defaults (120 s restart, 20 s grace)", st["config"]["daemon_restart_after_seconds"] == 120 and st["config"]["daemon_stop_grace_seconds"] == 20, (st["config"]["daemon_restart_after_seconds"], st["config"]["daemon_stop_grace_seconds"]))
c, t = api("POST", "/api/config", {"daemon_restart_after_seconds": 30, "daemon_stop_grace_seconds": 40}); cfg = open("/data/Transmission/config.cfg").read()
check("new settings are applied and saved to config.cfg", c == 200 and "daemon_restart_after_seconds=30" in cfg and "daemon_stop_grace_seconds=40" in cfg and "restart_elf_path" not in cfg, t[:80])
c, t = api("POST", "/api/config", {"daemon_stop_grace_seconds": 1, "poll_interval_seconds": 9}); st = json.loads(api("GET", "/api/state")[1])
check("an out-of-range value is rejected as a whole (nothing else changed either)", c == 400 and st["config"]["daemon_stop_grace_seconds"] == 40 and st["config"]["poll_interval_seconds"] == 7, t[:90])
c, t = api("POST", "/api/config", {"daemon_restart_after_seconds": 99999}); check("restart threshold above 3600 is rejected", c == 400)
check("text/plain POST is refused (CSRF guard)", post_raw("/api/daemon", '{"action":"stop"}', "text/plain") == 415)
check("an unknown daemon action is refused", api("POST", "/api/daemon", {"action": "explode"})[0] == 400)

print("== sleep test button (diagnostics): immediate sleep")
api("POST", "/api/config", {"sleep_countdown_seconds": 3})
api("POST", "/api/sleep", {"action": "start"}); time.sleep(8); calls = open(CALLS).read() if os.path.exists(CALLS) else ""
check("the rest-mode function is called (Transmission stopped first)", "Suspend" in calls, calls.strip().split("\n")[-1] if calls else "no calls")

for pid in mgr_pids(): os.kill(pid, signal.SIGCONT)       # the console woke up: the daemon starts again
wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 60); time.sleep(2)
print("== diagnostics on the new manager (they look at the DAEMON process)")
c, t = api("GET", "/api/diag/threads"); txt = json.loads(t)["text"]
check("thread snapshot is of the daemon process", c == 200 and "tid=" in txt and f"pid {daemon_pids()[0]}" in txt, txt.strip().split("\n")[-1])
c, t = api("GET", "/api/diag/probe"); check("probe: Transmission answers", "connect OK" in json.loads(t)["text"] and "409" in json.loads(t)["text"], json.loads(t)["text"])
for path, needle in (("/api/diag/log", "daemon: running"), ("/api/diag/dlog", "daemon.log"), ("/api/diag/tlog", "transmission.log"), ("/api/diag/all", "=== daemon wrapper log")):
    c, t = api("GET", path); check(f"{path}", c == 200 and needle in t, t.split("\n")[0][:60])
kill_all(); loader.close()
sys.exit(summary())
