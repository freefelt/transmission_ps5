import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, time, os, base64, hashlib, urllib.request, urllib.error
from harness import *
def jrpc(method, params=None):
    sid = ""
    for _ in range(3):
        req = urllib.request.Request("http://127.0.0.1:9091/transmission/rpc", data=json.dumps({"jsonrpc": "2.0", "method": method, "params": params or {}, "id": 1}).encode(), headers={"Content-Type": "application/json", "X-Transmission-Session-Id": sid})
        try: return json.loads(urllib.request.urlopen(req, timeout=10).read())
        except urllib.error.HTTPError as e:
            if e.code == 409: sid = e.headers.get("X-Transmission-Session-Id"); continue
            return json.loads(e.read() or b"{}")
def b64(d): return base64.b64encode(bencode(d)).decode()
pcs = hashlib.sha1(b"x").digest()
plain = {"announce": "http://127.0.0.1:1/announce", "info": {"name": "if_plain", "length": 65536, "piece length": 65536, "pieces": pcs}}
infolder = {"announce": "http://127.0.0.1:1/announce", "info": {"name": "if_folder1", "files": [{"length": 65536, "path": ["data.bin"]}], "piece length": 65536, "pieces": pcs}}
multi = {"announce": "http://127.0.0.1:1/announce", "info": {"name": "if_multi", "files": [{"length": 65536, "path": ["a.bin"]}, {"length": 65536, "path": ["sub", "b.bin"]}], "piece length": 65536, "pieces": pcs + pcs}}
fresh(); loader = Loader(); m = start_manager()
try:
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(3)
    for d in (plain, infolder, multi): rpc("torrent-add", metainfo=b64(d), paused=True)
    ts = {t["name"]: t for t in jrpc("torrent_get", {"fields": ["name", "file_count", "ps5_in_folder"]})["result"]["torrents"]}
    check("a plain single-file torrent: ps5_in_folder is false", ts["if_plain"]["file_count"] == 1 and ts["if_plain"]["ps5_in_folder"] is False, ts["if_plain"])
    check("a torrent with ONE file inside a folder (what the stock web client shows as a file): ps5_in_folder is TRUE", ts["if_folder1"]["file_count"] == 1 and ts["if_folder1"]["ps5_in_folder"] is True, ts["if_folder1"])
    check("a multi-file torrent: true as well", ts["if_multi"]["file_count"] == 2 and ts["if_multi"]["ps5_in_folder"] is True, ts["if_multi"])
    files = jrpc("torrent_get", {"ids": [t["id"] for t in jrpc("torrent_get", {"fields": ["id", "name"]})["result"]["torrents"] if t["name"] == "if_folder1"], "fields": ["files"]})["result"]["torrents"][0]["files"]
    check("(the files list confirms it: the file path is 'if_folder1/data.bin', i.e. a folder and a file in it)", files[0]["name"] == "if_folder1/data.bin", files)
    r = jrpc("torrent_get", {"fields": ["name", "no_such_field"]})
    check("an unknown field is still ignored, not an error (nothing else was opened)", "result" in r and all(set(t) == {"name"} for t in r["result"]["torrents"]), str(r)[:120])
    old = rpc("torrent-get", fields=["name", "downloadDir"])["arguments"]["torrents"]
    check("the legacy API (camelCase fields) is unaffected", len(old) == 3 and all("downloadDir" in t for t in old), old[:1])
finally:
    kill_all()
    try: loader.close()
    except Exception: pass
sys.exit(summary())
