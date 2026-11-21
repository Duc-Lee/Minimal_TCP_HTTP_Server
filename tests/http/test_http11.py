#!/usr/bin/env python3
# kiem tra http/1.1 keep-alive va worker pool

from concurrent.futures import ThreadPoolExecutor
import os
import socket
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu
from common.http import read_http_response

HOST = "127.0.0.1"
PORT = 20080

def test_index_and_mime():
    print("test_index_and_mime:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(b"GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr, body = read_http_response(s)
    s.close()
    assert "200 OK" in hdr
    assert "text/html" in hdr
    assert len(body) > 0
    print("ok")

def test_favicon_204():
    print("test_favicon_204:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(b"GET /favicon.ico HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr, body = read_http_response(s)
    s.close()
    assert "204 No Content" in hdr
    assert len(body) == 0
    print("ok")

def test_keep_alive_multiple():
    print("test_keep_alive:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)

    s.sendall(b"GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    hdr1, body1 = read_http_response(s)
    assert "200 OK" in hdr1 and len(body1) > 0

    s.sendall(b"GET /favicon.ico HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    hdr2, body2 = read_http_response(s)
    assert "204 No Content" in hdr2 and len(body2) == 0

    s.sendall(b"GET /README HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr3, body3 = read_http_response(s)
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
    hdr, _ = read_http_response(s)
    s.close()
    assert "200 OK" in hdr
    print("ok")

def test_worker_concurrency():
    print("test_worker_pool:", end=" ", flush=True)

    def fetch(_):
        try:
            s = socket.create_connection((HOST, PORT), timeout=5)
            s.sendall(b"GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
            hdr, body = read_http_response(s)
            s.close()
            return "200 OK" in hdr and len(body) > 0
        except Exception:
            return False

    with ThreadPoolExecutor(max_workers=8) as pool:
        results = list(pool.map(fetch, range(8)))

    assert all(results)
    print("ok")

def main():
    proc = start_qemu()
    try:
        test_index_and_mime()
        test_favicon_204()
        test_keep_alive_multiple()
        test_long_browser_headers()
        test_worker_concurrency()
        print("all tests passed")
        return 0
    finally:
        stop_qemu(proc)

if __name__ == "__main__":
    sys.exit(main())
