import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from harness import *
def blocker(port):
    b = socket.socket(); b.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); b.bind(("0.0.0.0", port)); b.listen(4); return b
def journal(): c, t = api("GET", "/api/notifications"); return json.loads(t) if c == 200 else {"latest": -1, "items": []}
def texts(): return [i["text"] for i in journal()["items"]]
fresh(); b1 = blocker(9092); loader = Loader(); m = start_manager(); PORT["api"] = 9093
print("== 1) the configured settings port 9092 is taken by another program")
ok = wait(lambda: dstate() is not None, 30); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70)
st = json.loads(api("GET", "/api/state")[1])
check("the settings page came up on the first free port, 9093", ok and st["settings_port_actual"] == 9093 and st["config"]["settings_port"] == 9092, (st["settings_port_actual"], st["config"]["settings_port"]))
LINE = "listening on port 9093 (the configured port is busy"
check("the log says why (in the log file or, if the file was rotated at the start, in the memory ring of the log)", wait(lambda: LINE in shim() or LINE in api("GET", "/api/diag/log")[1], 10))
tx = texts(); line = [t for t in tx if "Settings page: http://" in t and ":9093/" in t]
check("the notification names the port: 'Settings page: http://IP:9093/ (port 9092 is busy ...)'", bool(line) and "9092 is busy" in line[0], line[:1])
check("the 'Transmission started' notification carries the actual settings URL (9093)", any("Transmission started" in t and ":9093/" in t for t in tx), "")

print("== 2) the Transmission port is changed in the settings: the daemon restarts on the new port")
ida, ha = add_torrent("A_active.bin", False); idc, hc = add_torrent("C_paused_by_user.bin", True); time.sleep(3)
pid0 = daemon_pids()[0]
c, t = api("POST", "/api/config", {"daemon_port": 9093}); check("the port of the settings page is refused", c == 400 and "занят страницей настроек" in t, t[:100])
b2 = blocker(9301); c, t = api("POST", "/api/config", {"daemon_port": 9301}); check("a port taken by another program is refused", c == 400 and "занят другой программой" in t, t[:100]); b2.close()
c, t = api("POST", "/api/config", {"daemon_port": 80}); check("a port outside 1024-65535 is refused", c == 400)
check("nothing changed after the refusals (same daemon, same port)", daemon_pids() == [pid0] and json.loads(api("GET", "/api/state")[1])["rpc_port"] == 9091)
c, t = api("POST", "/api/config", {"daemon_port": 9195}); check("a free port is accepted", c == 200, t[:80])
PORT["rpc"] = 9195
ok = wait(lambda: daemon_pids() and daemon_pids()[0] != pid0 and rpc_ok() and dstate()["state"] == "running", 100)
check("the daemon restarted by itself and now answers on 9195", ok and not any(True for _ in [0] if False), f"pids {pid0}->{daemon_pids()}")
try: old = urllib.request.urlopen("http://127.0.0.1:9091/transmission/web/", timeout=3).status
except Exception: old = None
check("nothing listens on the old port 9091 any more", old is None)
time.sleep(4); st = torrents()
check("torrents came back: the active one is active, the paused one stays paused", st.get("A_active.bin", 0) != 0 and st.get("C_paused_by_user.bin", 1) == 0, st)
check("the page reports the new port; the port file and config.cfg agree", json.loads(api("GET", "/api/state")[1])["rpc_port"] == 9195 and open("/data/Transmission/transmission/daemon.port").read().strip() == "9195" and "daemon_port=9195" in open("/data/Transmission/config.cfg").read())

print("== 3) the notification journal: everything is there, including notifications switched off in the settings")
time.sleep(14)                                        # 10 "quiet" seconds after the daemon restart: events are not tracked yet
before = journal()["latest"]
c, t = api("POST", "/api/config", {"notify_torrent_started": False}); time.sleep(1)
add_torrent("D_new_download.bin", False)
ok = wait(lambda: any("Started" in i["text"] and "D_new_download" in i["text"] for i in journal()["items"]), 40)
it = [i for i in journal()["items"] if "D_new_download" in i["text"] and "Started" in i["text"]]
check("a switched-off 'Started' notification is in the journal, marked as not sent", ok and it and it[0]["shown"] is False, it[:1])
check("and it was NOT sent to the console", "NOTIFY: Transmission\nStarted: D_new_download" not in mgrlog() and "Started: D_new_download" not in mgrlog())
api("POST", "/api/config", {"notify_torrent_started": True}); time.sleep(1); add_torrent("E_new_download.bin", False)
ok = wait(lambda: any("E_new_download" in i["text"] and i["shown"] for i in journal()["items"]), 40)
check("with the setting on the next one is sent and marked as sent", ok and "Started: E_new_download" in mgrlog(), "")
j = journal(); st = json.loads(api("GET", "/api/state")[1])
check("the page learns about new notifications through 'notif_latest'", st["notif_latest"] == j["latest"] and j["latest"] > before, (st["notif_latest"], j["latest"]))
n_before = len(j["items"])
for p in mgr_pids(): os.kill(p, signal.SIGKILL)
time.sleep(1); m2 = start_manager("mgr_j"); wait(lambda: dstate() is not None, 40)
j2 = journal(); check("the journal survives a manager restart", len(j2["items"]) >= n_before and any("E_new_download" in i["text"] for i in j2["items"]), (n_before, len(j2["items"])))
c, t = api("POST", "/api/notifications", {"action": "clear"}); j3 = journal()
check("'Очистить' empties the journal (numbering continues)", c == 200 and not j3["items"] and j3["latest"] >= j2["latest"], j3)
kill_all(); loader.close(); b1.close()
sys.exit(summary())
