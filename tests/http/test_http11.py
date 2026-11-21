#!/usr/bin/env python3
# kiem tra http/1.1 keep-alive va worker pool

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
import socket
import sys

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu
from common.http import read_http_response

def test_index_and_mime(host, port):
    print("test_index_and_mime:", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=5)
    s.sendall(f"GET / HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr, body = read_http_response(s)
    s.close()
    assert "200 OK" in hdr
    assert "text/html" in hdr
    assert len(body) > 0
    print("ok")

def test_favicon_204(host, port):
    print("test_favicon_204:", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=5)
    s.sendall(f"GET /favicon.ico HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr, body = read_http_response(s)
    s.close()
    assert "204 No Content" in hdr
    assert len(body) == 0
    print("ok")

def test_keep_alive_multiple(host, port):
    print("test_keep_alive:", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=5)

    s.sendall(f"GET /index.html HTTP/1.1\r\nHost: {host}\r\n\r\n".encode())
    hdr1, body1 = read_http_response(s)
    assert "200 OK" in hdr1 and len(body1) > 0

    s.sendall(f"GET /favicon.ico HTTP/1.1\r\nHost: {host}\r\n\r\n".encode())
    hdr2, body2 = read_http_response(s)
    assert "204 No Content" in hdr2 and len(body2) == 0

    s.sendall(f"GET /README HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr3, body3 = read_http_response(s)
    assert "200 OK" in hdr3 and "text/plain" in hdr3

    extra = s.recv(10)
    assert extra == b""
    s.close()
    print("ok")

def test_long_browser_headers(host, port):
    print("test_long_headers:", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=5)
    cookie = "token=" + ("a" * 1200)
    req = (
        f"GET /index.html HTTP/1.1\r\n"
        f"Host: {host}\r\n"
        f"Connection: close\r\n"
        f"Cookie: {cookie}\r\n\r\n"
    )
    s.sendall(req.encode())
    hdr, _ = read_http_response(s)
    s.close()
    assert "200 OK" in hdr
    print("ok")

def test_worker_concurrency(host, port):
    print("test_worker_pool:", end=" ", flush=True)

    def fetch(_):
        try:
            s = socket.create_connection((host, port), timeout=5)
            s.sendall(f"GET /index.html HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
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
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20080)
    args = parser.parse_args()

    proc = start_qemu()
    try:
        test_index_and_mime(args.host, args.port)
        test_favicon_204(args.host, args.port)
        test_keep_alive_multiple(args.host, args.port)
        test_long_browser_headers(args.host, args.port)
        test_worker_concurrency(args.host, args.port)
        print("all tests passed")
        return 0
    finally:
        stop_qemu(proc)

if __name__ == "__main__":
    sys.exit(main())
