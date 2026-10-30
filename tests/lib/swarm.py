"""A minimal seeder and tracker in Python: so that on the host the real daemon downloads a torrent over the network (127.0.0.1) and one can watch
in what order the pieces are written to the file. The seeder gives away all pieces and answers interested/request."""
import socket, struct, threading, hashlib, os, http.server, socketserver, time
class Seeder:
    def __init__(self, path, piece_len, n_pieces, info_hash, rate_bps=None, have=None, log=None):
        self.path, self.pl, self.n, self.ih, self.rate = path, piece_len, n_pieces, info_hash, rate_bps
        self.have = have if have is not None else (0, n_pieces)
        self.sock = socket.socket(); self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); self.sock.bind(("0.0.0.0", 0)); self.sock.listen(16)
        self.port = self.sock.getsockname()[1]; self.stop_flag = False; self.requests = log if log is not None else []; self.lock = threading.Lock()
        threading.Thread(target=self.accept_loop, daemon=True).start()
    def accept_loop(self):
        while not self.stop_flag:
            try: c, _ = self.sock.accept()
            except OSError: return
            threading.Thread(target=self.handle, args=(c,), daemon=True).start()
    @staticmethod
    def recv_exact(c, n):
        b = b""
        while len(b) < n:
            d = c.recv(n - len(b))
            if not d: raise EOFError
            b += d
        return b
    def send(self, c, mid, payload=b""): c.sendall(struct.pack(">I", 1 + len(payload)) + bytes([mid]) + payload)
    def handle(self, c):
        try:
            c.settimeout(60); hs = self.recv_exact(c, 68)
            if hs[28:48] != self.ih: return
            c.sendall(b"\x13BitTorrent protocol" + b"\0" * 8 + self.ih + b"-SW0001-" + os.urandom(6).hex().encode()[:12])
            bf = bytearray((self.n + 7) // 8)
            for i in range(self.have[0], self.have[1]): bf[i // 8] |= 0x80 >> (i % 8)
            self.send(c, 5, bytes(bf)); fd = os.open(self.path, os.O_RDONLY)
            while not self.stop_flag:
                ln = struct.unpack(">I", self.recv_exact(c, 4))[0]
                if ln == 0: continue
                m = self.recv_exact(c, ln); mid = m[0]
                if mid == 2: self.send(c, 1)
                elif mid == 6:
                    idx, beg, length = struct.unpack(">III", m[1:13])
                    with self.lock: self.requests.append(idx)
                    data = os.pread(fd, length, idx * self.pl + beg); self.send(c, 7, struct.pack(">II", idx, beg) + data)
                    if self.rate: time.sleep(length / self.rate)
        except Exception: pass
        finally:
            try: c.close()
            except Exception: pass
    def stop(self):
        self.stop_flag = True
        try: self.sock.close()
        except Exception: pass
class Tracker:
    def __init__(self, peers, peer_ip="192.0.2.2"):
        """peers: list of the seeder ports (all on peer_ip)."""
        blob = b"".join(socket.inet_aton(peer_ip) + struct.pack(">H", p) for p in peers)
        class H(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = b"d8:completei1e10:incompletei0e8:intervali30e12:min intervali10e5:peers" + str(len(blob)).encode() + b":" + blob + b"e"
                self.send_response(200); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
            def log_message(self, *a): pass
        self.srv = socketserver.ThreadingTCPServer(("127.0.0.1", 0), H); self.srv.daemon_threads = True; self.port = self.srv.server_address[1]
        threading.Thread(target=self.srv.serve_forever, daemon=True).start()
    def stop(self): self.srv.shutdown(); self.srv.server_close()

def make_torrent(bencode, seed_dir, name, size, piece_len):
    """Creates the data file and the info dictionary; returns (info, info_hash, the path to the data, the number of pieces). announce is added separately (the hash does not depend on it)."""
    os.makedirs(seed_dir, exist_ok=True); path = os.path.join(seed_dir, name); pieces = b""; n = (size + piece_len - 1) // piece_len
    with open(path, "wb") as f:
        for i in range(n):
            chunk = os.urandom(min(piece_len, size - i * piece_len)); f.write(chunk); pieces += hashlib.sha1(chunk).digest()
    info = {"name": name, "length": size, "piece length": piece_len, "pieces": pieces}
    return info, hashlib.sha1(bencode(info)).digest(), path, n


def make_multi(bencode, seed_dir, name, sizes, piece_len):
    """A torrent of several files: the data lie at the seeder as one file (the pieces follow the concatenation of the files)."""
    os.makedirs(seed_dir, exist_ok=True); path = os.path.join(seed_dir, name + ".concat"); total = sum(sizes); pieces = b""; n = (total + piece_len - 1) // piece_len
    with open(path, "wb") as f:
        for i in range(n):
            chunk = os.urandom(min(piece_len, total - i * piece_len)); f.write(chunk); pieces += hashlib.sha1(chunk).digest()
    info = {"name": name, "piece length": piece_len, "pieces": pieces, "files": [{"length": L, "path": ["f%d.bin" % k]} for k, L in enumerate(sizes)]}
    return info, hashlib.sha1(bencode(info)).digest(), path, n
