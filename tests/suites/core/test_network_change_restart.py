import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, signal, time, os, re
from harness import *
import harness; harness.AUTO_FOLDER = False
IPF = "/tmp/ps5tm_test_ip"; DLOG = "/data/Transmission/logs/daemon.log"
def bell(): c, t = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(t)["items"]] if c == 200 else []
def netrestarts(): return [x for x in bell() if "перезапущен после смены сети" in x]
def log_has(s): return s in shim()
if os.path.exists(IPF): os.remove(IPF)
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(4)
    api("POST", "/api/config", {"daemon_restart_after_seconds": 600, "daemon_stop_grace_seconds": 5, "daemon_network_restart_seconds": 8})
    check("the new setting exists: 15 s by default is written into config.cfg with its explanation, and the page state has it", "daemon_network_restart_seconds=" in open("/data/Transmission/config.cfg").read() and json.loads(api("GET", "/api/state")[1])["config"]["daemon_network_restart_seconds"] == 8, None)
    pid0 = daemon_pids()[0]
    print("== 1) the network changes, but Transmission answers on the new address: nothing is restarted")
    open(IPF, "w").write("127.0.0.2\n"); time.sleep(25)
    check("the IP changed (127.0.0.2), the daemon answers there: the same process keeps running, the log says there was nothing to restart", daemon_pids() == [pid0] and log_has("Transmission answers on 127.0.0.2") and not netrestarts(), (daemon_pids(), [l for l in shim().split("\n") if "network:" in l][-3:]))
    print("== 2) the network changes and Transmission does NOT answer on the new address: restarted after about 8 s, not after the general timeout")
    t0 = time.time(); open(IPF, "w").write("192.0.2.55\n")
    ok = wait(lambda: daemon_pids() and daemon_pids()[0] != pid0 and rpc_ok() and dstate()["state"] == "running", 60); took = time.time() - t0; time.sleep(3)
    check("the daemon was restarted about 8-30 s after the change (the general rule is set to 600 s here, so only the new rule can explain it)", ok and 7 <= took <= 35, f"{took:.0f} s")
    bl = netrestarts()
    check("the bell says so once: 'Transmission перезапущен после смены сети (сменился IP-адрес): на адресе 192.0.2.55 он не отвечал N с'", len(bl) == 1 and "сменился IP-адрес" in bl[0] and "192.0.2.55" in bl[0] and re.search(r"не отвечал (\d+ мин )?\d+ с", bl[0]) is not None, bl)
    check("no second entry about the same case ('простой') and no second restart while the new address stays unreachable (one attempt per network event)", not [x for x in bell() if "перезапущен: простой" in x], [x[:60] for x in bell()][-4:])
    pid1 = daemon_pids()[0]; time.sleep(30)
    check("after 30 more seconds the process is still the same (no restart loop)", daemon_pids() == [pid1], daemon_pids())
    print("== 3) the setting 0 turns the new behaviour off")
    api("POST", "/api/config", {"daemon_network_restart_seconds": 0}); open(IPF, "w").write("192.0.2.56\n"); time.sleep(25)
    check("with 0 nothing is restarted after a network change", daemon_pids() == [pid1] and len(netrestarts()) == 1, (daemon_pids(), len(netrestarts())))
    print("== 4) the daemon is waiting for a disk: a restart would not help and could do harm")
    api("POST", "/api/config", {"daemon_network_restart_seconds": 8}); open(IPF, "w").write("127.0.0.3\n"); time.sleep(12)
    with open(DLOG, "a") as f: f.write("[ 99.000] io-watch: blocked 12 write off=0 len=1 path=/mnt/usb1/x\n")
    time.sleep(3); open(IPF, "w").write("192.0.2.57\n"); time.sleep(25)
    check("while the daemon log says 'io-watch: blocked' (it waits for a disk) the network rule does not restart it", daemon_pids() == [pid1] and len(netrestarts()) == 1, (daemon_pids(), len(netrestarts())))
    with open(DLOG, "a") as f: f.write("[ 130.000] io-watch: done 31.0 write off=0 len=1 path=/mnt/usb1/x\n")
    ok = wait(lambda: daemon_pids() and daemon_pids()[0] != pid1 and rpc_ok() and dstate()["state"] == "running", 60)
    check("after 'io-watch: done' the pending check goes on and the daemon is restarted", ok and len(netrestarts()) == 2, (daemon_pids(), len(netrestarts())))
    print("== 5) the network disappears and comes back with the same address: the daemon answers, nothing happens")
    os.remove(IPF); time.sleep(6); pid2 = daemon_pids()[0]; open(IPF, "w").write("none\n"); time.sleep(4); os.remove(IPF); time.sleep(20)
    check("lost and back with the same IP: the daemon answers, no restart", daemon_pids() == [pid2] and log_has("network dropped and came back") and len(netrestarts()) == 2, (daemon_pids(), len(netrestarts())))
finally:
    if os.path.exists(IPF): os.remove(IPF)
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
