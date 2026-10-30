import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, re
from harness import *
# The messages of the wrapper itself (a second start replaces a running manager): the bell records them as "system" (kind 3, kind 4 when the start notifications are switched off in the settings).
fresh(); loader = Loader(); m1 = start_manager("mgr1")
def entries(): return [i for i in json.loads(api("GET", "/api/notifications")[1])["items"] if "is already running" in i["text"]]
try:
    wait(lambda: daemon_pids() and rpc_ok(), 70); time.sleep(3)
    check("a normal start has no 'already running' message", entries() == [], entries())
    m2 = start_manager("mgr2"); wait(lambda: entries(), 40); time.sleep(2)
    e = entries()
    check("the second start replaces the running manager and the bell records 'Transmission is already running: restarting it (pid N)' as a SYSTEM entry (kind 3: sent to the console, marked)", len(e) == 1 and e[0]["kind"] == 3 and re.search(r"restarting it \(pid \d+\)", e[0]["text"]), e)
    c, t = api("POST", "/api/config", {"notify_startup": False}); check("the start notifications are switched off in the settings", c == 200, t[:80])
    m3 = start_manager("mgr3"); wait(lambda: len(entries()) >= 2, 40); time.sleep(2)
    e = entries()
    check("with the start notifications off the same message is still in the bell: a system entry that is marked as not sent (kind 4); the first entry is unchanged", len(e) == 2 and e[0]["kind"] == 3 and e[1]["kind"] == 4, [(x["kind"], x["text"][:50]) for x in e])
finally:
    kill_all(); loader.close()
sys.exit(summary())
