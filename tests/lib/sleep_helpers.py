import sys
from harness import *
CALLS = "/tmp/ps5tm_host_calls.log"
def suspends():
    return open(CALLS).read().count("NotifySystemSuspendStart") if os.path.exists(CALLS) else 0
def sstate(): return json.loads(api("GET", "/api/state")[1])
def wake(): 
    for p in mgr_pids(): os.kill(p, signal.SIGCONT)
def setup(extra_cfg=None):
    fresh()
    if os.path.exists(CALLS): os.remove(CALLS)
    loader = Loader(); m = start_manager()
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(6)
    ida, ha = add_torrent("A_active.bin", False); idc, hc = add_torrent("C_paused_by_user.bin", True); time.sleep(2)
    api("POST", "/api/config", {"sleep_countdown_seconds": 3})
    return loader, m, ha, hc

import hashlib, base64
def add_real_torrent(name, data):
    """a torrent whose data is not on the disk yet: it downloads; then finish_real_torrent puts the file there and verifies"""
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": len(data), "piece length": 65536, "pieces": hashlib.sha1(data).digest()}})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=False)["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate")
    return t["id"]
def finish_real_torrent(tid, name, data):
    os.makedirs("/data/torrents", exist_ok=True); open(f"/data/torrents/{name}", "wb").write(data); rpc("torrent-verify", ids=[tid])
