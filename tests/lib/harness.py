import base64, hashlib, json, os, shutil, signal, socket, subprocess, threading, time, urllib.request, urllib.error
HOST_DIR = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HOST_DIR))
WEB_TESTS = os.path.join(REPO, "tests", "web")
OUT = os.path.join(REPO, ".deps", "test", "out"); MGR = f"{OUT}/Transmission_mgr_host"
PORT = {"api": 9092, "rpc": 9091}
res = []
def check(name, ok, extra=""):
    res.append(bool(ok)); print(("PASS " if ok else "FAIL ") + name + (("  -> " + str(extra)) if extra != "" else ""), flush=True)
def summary():
    print(f"\n{sum(res)}/{len(res)} checks passed"); return 0 if all(res) else 1
def pids(comm):
    out = []
    for d in os.listdir("/proc"):
        if d.isdigit():
            try:
                if open(f"/proc/{d}/comm").read().strip() == comm and open(f"/proc/{d}/stat").read().split(") ")[1][0] != "Z": out.append(int(d))
            except Exception: pass
    return out
daemon_pids = lambda: pids("transmission_d.")
mgr_pids = lambda: pids("Transmission.el")
class Loader:                                     # the console's "ELF loader": takes bytes on 9021 and executes them
    def __init__(self):
        self.count = 0; self.icon_count = 0; self.seq = 0; self.sizes = []; self.mode = "run"; os.makedirs("/tmp/ldr", exist_ok=True)
        self.srv = socket.socket(); self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); self.srv.bind(("127.0.0.1", 9021)); self.srv.listen(4)
        threading.Thread(target=self.loop, daemon=True).start()
    def loop(self):
        while True:
            try: c, _ = self.srv.accept()
            except OSError: return
            data = b""
            while True:
                d = c.recv(1 << 20)
                if not d: break
                data += d
            c.close(); self.seq += 1
            if len(data) >= (1 << 20): self.count += 1; self.sizes.append(len(data))     # daemon
            else: self.icon_count += 1                                                    # the small icon-installation helper
            if self.mode == "swallow": continue                    # the loader accepted the ELF and started nothing
            path = f"/tmp/ldr/run_{self.seq}.elf"; open(path, "wb").write(data); os.chmod(path, 0o755)
            pr = subprocess.Popen([path], cwd="/tmp", stdout=open(f"/tmp/ldr/run_{self.seq}.out", "w"), stderr=subprocess.STDOUT, start_new_session=True)
            if self.mode == "die": threading.Timer(0.6, pr.kill).start()   # the process started and died at once
    def close(self):
        try: self.srv.shutdown(socket.SHUT_RDWR)
        except OSError: pass
        self.srv.close(); time.sleep(0.3)
def api(method, path, body=None, timeout=20):
    data = json.dumps(body).encode() if body is not None else None
    r = urllib.request.Request(f"http://127.0.0.1:{PORT['api']}{path}", data=data, method=method)
    if data is not None: r.add_header("Content-Type", "application/json")
    try: resp = urllib.request.urlopen(r, timeout=timeout); return resp.status, resp.read().decode()
    except urllib.error.HTTPError as e: return e.code, e.read().decode()
    except Exception as e: return 0, str(e)
def dstate():
    c, t = api("GET", "/api/state")
    return json.loads(t)["daemon"] if c == 200 else None
_sid = [""]
def rpc(method, timeout=6, **args):
    for _ in range(2):
        r = urllib.request.Request(f"http://127.0.0.1:{PORT['rpc']}/transmission/rpc", data=json.dumps({"method": method, "arguments": args}).encode(), headers={"X-Transmission-Session-Id": _sid[0]})
        try: return json.load(urllib.request.urlopen(r, timeout=timeout))
        except urllib.error.HTTPError as e:
            if e.code == 409: _sid[0] = e.headers["X-Transmission-Session-Id"]; continue
            raise
def rpc_ok():
    try: return rpc("session-get", timeout=3)["result"] == "success"
    except Exception: return False
def wait(cond, seconds, step=1.0):
    t = time.time()
    while time.time() - t < seconds:
        try:
            if cond(): return True
        except Exception: pass
        time.sleep(step)
    return False
def bencode(x):
    if isinstance(x, int): return b"i%de" % x
    if isinstance(x, bytes): return b"%d:%s" % (len(x), x)
    if isinstance(x, str): return bencode(x.encode())
    if isinstance(x, list): return b"l" + b"".join(bencode(i) for i in x) + b"e"
    if isinstance(x, dict): return b"d" + b"".join(bencode(k) + bencode(v) for k, v in sorted(x.items())) + b"e"
def add_torrent(name, paused):
    pieces = hashlib.sha1(name.encode()).digest()
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": pieces}})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=paused)["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate")
    return t["id"], t["hashString"]
def torrents():
    return {t["name"]: t["status"] for t in rpc("torrent-get", fields=["name", "status"])["arguments"]["torrents"]}
def kill_all():
    for comm in ("Transmission.el", "transmission_d."):
        subprocess.run(["pkill", "-9", "-x", comm], stderr=subprocess.DEVNULL)
    time.sleep(1.5)
AUTO_FOLDER = True   # after the download folders guard a torrent cannot be added without a folder in the list: by default a clean installation has one folder /data/torrents (the host's internal disk)
BASE_CFG = "download_folder=/data/torrents\ndownload_folders_migrated=yes\n"
def fresh(keep_data=False):
    kill_all()
    if not keep_data:
        shutil.rmtree("/data/Transmission", ignore_errors=True)
        if AUTO_FOLDER:
            os.makedirs("/data/Transmission", exist_ok=True); open("/data/Transmission/config.cfg", "w", encoding="utf-8").write(BASE_CFG)
    shutil.rmtree("/tmp/ldr", ignore_errors=True)
    if os.path.exists("/tmp/ps5tm_test_ip"): os.remove("/tmp/ps5tm_test_ip")
TEST_MSG = "mixed"   # language of the messages in the checks (None: the selected language, as in a real installation)
TEST_LANG = "ru"   # default language of the settings page in the checks (None: as in a real installation, that is English)
SYS_HELPER = False   # True: the manager starts the system service helper itself on start (off by default in the checks so it does not get in the way of the others)
def start_manager(tag="mgr"):
    env = dict(os.environ)
    if not SYS_HELPER: env["PS5TM_NO_SYS_HELPER"] = "1"
    env["PS5TM_TEST_POWEROFF"] = "1"   # powering the console off in the checks: only a marker /tmp/ps5tm_test_poweroff
    if TEST_LANG: env["PS5TM_TEST_LANG"] = TEST_LANG
    if TEST_MSG: env["PS5TM_TEST_MSG"] = TEST_MSG   # "mixed": messages as before the translation (me_ English, mr_ Russian) so the old text checks do not change   # powering the console off in the checks: only a marker /tmp/ps5tm_test_poweroff
    return subprocess.Popen([MGR], stdout=open(f"/tmp/{tag}.out", "w"), stderr=subprocess.STDOUT, cwd="/tmp", start_new_session=True, env=env)
mgrlog = lambda tag="mgr": open(f"/tmp/{tag}.out").read() if os.path.exists(f"/tmp/{tag}.out") else ""
shim = lambda: open("/data/Transmission/logs/shim.log").read()
