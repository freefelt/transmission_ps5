import socket, sys, time
def get(path="/api/state", delay=0.3, port=9092):
    s = socket.create_connection(("127.0.0.1", port), timeout=6); time.sleep(delay)   # like a real network: the request arrives after the connection
    s.sendall(f"GET {path} HTTP/1.0\r\nHost: x\r\n\r\n".encode()); data = b""
    try:
        while True:
            c = s.recv(4096)
            if not c: break
            data += c
    except Exception as e: return f"ERROR {e}"
    return data[:15].decode(errors="replace") if data else "EMPTY REPLY (connection closed with no data)"
print(" | ".join(get() for _ in range(3)))
