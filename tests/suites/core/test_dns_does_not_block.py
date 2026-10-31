import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, re, socket, threading, subprocess, base64, hashlib, time, json
from harness import *
# A DNS server that never answers (a home network that lets only some DNS servers through) must not hold up the start of the daemon (the DHT looked up its
# bootstrap name in the session thread) or its shutdown (the "stopped" announces of the trackers wait for name lookups).
REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
RESOLV = "/etc/resolv.conf"; BAK = "/tmp/ps5tm_resolv.bak"
def run(cmd): return subprocess.run(cmd, shell=True, capture_output=True, text=True)
def meta(name, tracker): return base64.b64encode(bencode({"announce": tracker, "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def seconds(pattern, text): m = re.findall(pattern, text); return float(m[-1]) if m else None
def dlog(): return open("/data/Transmission/logs/daemon.log", errors="replace").read() if os.path.exists("/data/Transmission/logs/daemon.log") else ""
stop_srv = threading.Event(); asked = []
def blackhole():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(("127.0.0.1", 53)); s.settimeout(0.5)
    while not stop_srv.is_set():
        try: data, _ = s.recvfrom(512); asked.append(data)
        except socket.timeout: pass
    s.close()
run(f"rm -f {BAK}; cp -a {RESOLV} {BAK}")
fresh(); loader = Loader(); m = start_manager()
try:
    print("== 1) the DNS works: nothing changes")
    ok = wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70)
    t_start = seconds(r"RPC answered ([\d.]+) s after the launch", shim())
    check("the daemon starts and answers RPC (a few seconds at most)", ok and t_start is not None and t_start <= 4, t_start)
    rpc("torrent-add", metainfo=meta("dns_ok", "http://localhost:1/announce"), paused=False); time.sleep(6)
    check("a tracker name that resolves is looked up as before: no 'dns:' lines in the daemon log", "dns:" not in dlog(), [l for l in dlog().split("\n") if "dns:" in l][:2])

    print("== 2) the DNS server does not answer at all (the resolver would wait 25 s for every name)")
    api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped", 60)
    srv = threading.Thread(target=blackhole, daemon=True); srv.start(); time.sleep(0.5)
    run(f"rm -f {RESOLV}; printf 'nameserver 127.0.0.1\\noptions timeout:25 attempts:1\\n' > {RESOLV}")
    t0 = time.time(); api("POST", "/api/daemon", {"action": "start"})
    ok = wait(lambda: dstate()["state"] == "running" and rpc_ok(), 70); t_total = time.time() - t0
    t_start = seconds(r"RPC answered ([\d.]+) s after the launch", shim())
    check("the daemon starts and answers RPC in a few seconds, not after the 25 s wait of the DHT lookup", ok and t_start is not None and t_start <= 6, f"{t_start} s (whole start {t_total:.0f} s)")
    check("the DHT lookup that timed out is given up in the background and written to the daemon log (the wrapper is in the daemon)", wait(lambda: "dns: 'dht.transmissionbt.com' did not answer in" in dlog(), 15), [l for l in dlog().split("\n") if "dns:" in l][:2])
    check("the daemon answers RPC all the time", all(rpc_ok() for _ in range(3)) and len(asked) > 0, f"{len(asked)} queries reached the silent server")
    print("== 3) the wrapper itself (a small program linked with it: the libcurl of the test machine is a shared library and is not covered by --wrap, on the console it is static)")
    r = run(f"clang-18 -O1 -D_GNU_SOURCE -DPS5TM_DAEMON -DPS5TM_HOST_TEST -DSHIM_VERSION=\\\"test\\\" -I{REPO}/src -I{REPO}/src/common -o /tmp/ps5tm_dns_limit {REPO}/tests/unit/dns_limit_test.c {REPO}/src/daemon/dns.c -lpthread -Wl,--wrap=getaddrinfo")
    check("the test program builds", r.returncode == 0, (r.stdout + r.stderr)[-300:])
    r = run("/tmp/ps5tm_dns_limit"); look = {}
    for l in r.stdout.split("\n"):
        w = l.split()
        if len(w) == 4 and w[0] == "lookup": look.setdefault(w[1], []).append((float(w[2]), w[3]))
    check("numbers and names from /etc/hosts are answered at once and unchanged", look.get("127.0.0.1", [(9, "")])[0] == (look["127.0.0.1"][0][0], "ok") and look["127.0.0.1"][0][0] < 0.5 and look.get("localhost", [(9, "")])[0][1] == "ok" and look["localhost"][0][0] < 1, look)
    check("a name that the DNS server does not answer: 'again' after about 4 s (the time limit), not after the 25 s of the resolver", all(3.5 <= look.get(n, [(0, "")])[0][0] <= 5.5 and look[n][0][1] == "again" for n in ("one.invalid", "two.invalid")), look)
    check("after two such failures the breaker is open: the next name fails at once", look.get("three.invalid", [(9, "")])[0][0] < 0.5 and look["three.invalid"][0][1] == "again", look.get("three.invalid"))
finally:
    stop_srv.set()
    run(f"rm -f {RESOLV}; cp -a {BAK} {RESOLV}")
    kill_all(); loader.close()
sys.exit(summary())
