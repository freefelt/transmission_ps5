import sys
from harness import *
import hashlib
def add_complete(name, paused=False, size=65536):
    data = os.urandom(size); os.makedirs("/data/torrents", exist_ok=True); open(f"/data/torrents/{name}", "wb").write(data)
    pieces = hashlib.sha1(data).digest()
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": size, "piece length": size, "pieces": pieces}})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=paused)["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate"); return t["id"]
def add_partial(name, have_pieces=3, total_pieces=8, piece=16384):
    data = os.urandom(total_pieces * piece); os.makedirs("/data/torrents", exist_ok=True)
    pieces = b"".join(hashlib.sha1(data[i * piece:(i + 1) * piece]).digest() for i in range(total_pieces))
    open(f"/data/torrents/{name}", "wb").write(data[: have_pieces * piece] + b"\0" * ((total_pieces - have_pieces) * piece))
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": total_pieces * piece, "piece length": piece, "pieces": pieces}})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=False)["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate"); return t["id"]
def add_complete_in(name, folder, size=65536):
    data = os.urandom(size); os.makedirs(folder, exist_ok=True); open(f"{folder}/{name}", "wb").write(data)
    pieces = hashlib.sha1(data).digest()
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": size, "piece length": size, "pieces": pieces}})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=False, **{"download-dir": folder})["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate"); return t["id"]
def add_multi(name, files=("a.bin", "b.bin"), size=32768):
    root = f"/data/torrents/{name}"; os.makedirs(root, exist_ok=True); blobs = []
    for f in files:
        d = os.urandom(size); open(f"{root}/{f}", "wb").write(d); blobs.append((f, d))
    allb = b"".join(d for _, d in blobs); piece = 32768
    pieces = b"".join(hashlib.sha1(allb[i:i + piece]).digest() for i in range(0, len(allb), piece))
    info = {"name": name, "piece length": piece, "pieces": pieces, "files": [{"length": len(d), "path": [f]} for f, d in blobs]}
    meta = base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": info})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=False)["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate"); return t["id"]
def add_tracker(name, announce, size=65536):
    folder = "/data/torrents"; os.makedirs(folder, exist_ok=True); data = os.urandom(size); open(f"{folder}/{name}", "wb").write(data)
    meta = base64.b64encode(bencode({"announce": announce, "info": {"name": name, "length": size, "piece length": size, "pieces": hashlib.sha1(data).digest()}})).decode()
    r = rpc("torrent-add", metainfo=meta, paused=False)["arguments"]; t = r.get("torrent-added") or r.get("torrent-duplicate"); return t["id"]
def setup():
    shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app"); shutil.rmtree("/data/torrents", ignore_errors=True)
    for d in ("/mnt/ext1", "/mnt/usb0"):   # drives for the download folders: real separate file systems, otherwise the guard considers the disk not connected
        os.system(f"umount {d} 2>/dev/null"); os.makedirs(d, exist_ok=True); os.system(f"mount -t tmpfs -o size=128m tmpfs {d}")
    fresh()
    with open("/data/Transmission/config.cfg", "a", encoding="utf-8") as cf: cf.write("download_folder=/tmp/ps5_usb1/torrents\ndownload_folder=/mnt/ext1/torrents\ndownload_folder=/mnt/usb0/torrents\n")
    loader = Loader(); m = start_manager(); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(5)
    rpc("session-set", **{"download-dir": "/data/torrents"})   # the stand's default folder (on the host the daemon would pick /mnt/ext1/torrents on the first start)
    add_complete("Seeding_complete.bin"); add_complete("Seeding_paused_complete.bin", paused=True); add_torrent("Downloading_nothing.bin", False)
    add_partial("Leeching_37_percent.bin", 3, 8); add_partial("Leeching_75_percent.bin", 6, 8)
    add_complete_in("On_usb1_seed.bin", "/tmp/ps5_usb1/torrents")
    add_multi("Folder_torrent")
    add_tracker("Long_tracker.bin", "http://very-long-tracker-hostname.openbittorrent.com:6969/announce")
    add_tracker("Longest_tracker.bin", "http://tracker.an-extremely-long-tracker-sitename-for-testing.org:6969/announce")
    add_complete_in("On_ext1_seed.bin", "/mnt/ext1/torrents")
    add_complete_in("On_usb0_seed.bin", "/mnt/usb0/torrents")
    for i in (1, 2): add_complete(f"Another_seed_{i}.bin")
    time.sleep(6); return loader
if __name__ == "__main__":
    loader = setup(); print("torrents:", torrents()); print("READY", flush=True)
    time.sleep(int(sys.argv[1]) if len(sys.argv) > 1 else 120)
    kill_all(); loader.close()
