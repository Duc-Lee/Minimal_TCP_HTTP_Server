#!/usr/bin/env python3
# kiem thu http/1.1 va worker pool (tuan 4 - tv3)

from concurrent.futures import ThreadPoolExecutor
import socket
import subprocess
import time

HOST = "127.0.0.1"
PORT = 20080

def start_qemu():
    cmd = [
        "qemu-system-riscv64",
        "-machine", "virt",
        "-bios", "none",
        "-kernel", "kernel/kernel",
        "-m", "128M",
        "-smp", "3",
        "-nographic",
        "-global", "virtio-mmio.force-legacy=false",
        "-drive", "file=fs.img,if=none,format=raw,id=x0",
        "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
        "-netdev", f"user,id=net0,hostfwd=tcp::{PORT}-:80",
        "-device", "e1000,netdev=net0,bus=pcie.0"
    ]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.5)
    proc.stdin.write(b"httpd\n")
    proc.stdin.flush()
    time.sleep(1.0)
    return proc

def read_headers_and_body(s):
    data = b""
    while b"\r\n\r\n" not in data:
        chunk = s.recv(1024)
        if not chunk:
            break
        data += chunk

    if b"\r\n\r\n" not in data:
        return data.decode("utf-8", "ignore"), b""

    hdr_part, body_part = data.split(b"\r\n\r\n", 1)
    hdr_text = hdr_part.decode("utf-8", "ignore")

    content_len = 0
    for line in hdr_text.splitlines():
        if line.lower().startswith("content-length:"):
            content_len = int(line.split(":", 1)[1].strip())

    while len(body_part) < content_len:
        chunk = s.recv(min(4096, content_len - len(body_part)))
        if not chunk:
            break
        body_part += chunk

    return hdr_text, body_part

def test_index_and_mime():
    print("test_index_and_mime:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr, body = read_headers_and_body(s)
    s.close()
    assert "200 OK" in hdr
    assert "text/html" in hdr
    assert len(body) > 0
    print("ok")

def test_favicon_204():
    print("test_favicon_204:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(b"GET /favicon.ico HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr, body = read_headers_and_body(s)
    s.close()
    assert "204 No Content" in hdr
    assert len(body) == 0
    print("ok")

def test_keep_alive_multiple():
    print("test_keep_alive:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)

    s.sendall(b"GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    hdr1, body1 = read_headers_and_body(s)
    assert "200 OK" in hdr1 and len(body1) > 0

    s.sendall(b"GET /favicon.ico HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    hdr2, body2 = read_headers_and_body(s)
    assert "204 No Content" in hdr2 and len(body2) == 0

    s.sendall(b"GET /README HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr3, body3 = read_headers_and_body(s)
    assert "200 OK" in hdr3 and "text/plain" in hdr3

    extra = s.recv(10)
    assert extra == b""
    s.close()
    print("ok")

def test_long_browser_headers():
    print("test_long_headers:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)
    cookie = "token=" + ("a" * 1200)
    req = (
        f"GET /index.html HTTP/1.1\r\n"
        f"Host: {HOST}\r\n"
        f"Connection: close\r\n"
        f"Cookie: {cookie}\r\n\r\n"
    )
    s.sendall(req.encode())
    hdr, _ = read_headers_and_body(s)
    s.close()
    assert "200 OK" in hdr
    print("ok")

def test_worker_concurrency():
    print("test_worker_pool:", end=" ", flush=True)

    def fetch(_):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.sendall(b"GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
            hdr, body = read_headers_and_body(s)
            s.close()
            return "200 OK" in hdr and len(body) > 0
        except Exception:
            return False

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(fetch, range(8)))

    assert all(results)
    print("ok")

def main():
    print("kiem thu http11 + worker pool tuan 4")
    proc = None
    try:
        s = socket.create_connection((HOST, PORT), timeout=0.5)
        s.close()
    except Exception:
        proc = start_qemu()

    try:
        test_index_and_mime()
        test_favicon_204()
        test_keep_alive_multiple()
        test_long_browser_headers()
        test_worker_concurrency()
        print("all tests passed")
    finally:
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()

if __name__ == "__main__":
    main()
