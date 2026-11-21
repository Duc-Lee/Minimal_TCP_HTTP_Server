
import argparse
import hashlib
import os
import socket
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu
from common.http import read_http_response

def test_sendfile_small(host, port):
    print("test_sendfile_small:", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=5)
    s.sendall(f"GET /README HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr, body = read_http_response(s)
    s.close()
    assert "200 OK" in hdr
    assert len(body) > 0
    print("ok")

def test_sendfile_1mb(host, port):
    print("test_sendfile_1mb:", end=" ", flush=True)
    t0 = time.time()
    s = socket.create_connection((host, port), timeout=15)
    s.sendall(f"GET /largefile.bin HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr, body = read_http_response(s, expected_bytes=1048576)
    s.close()
    t_elapsed = time.time() - t0

    assert "200 OK" in hdr
    assert len(body) == 1048576, f"so byte nhan: {len(body)}"

    with open("user/largefile.bin", "rb") as f:
        expected_sha = hashlib.sha256(f.read()).hexdigest()
    actual_sha = hashlib.sha256(body).hexdigest()
    assert actual_sha == expected_sha, "sai ma sha256"
    print(f"ok ({t_elapsed:.2f}s)")

def test_sendfile_keepalive(host, port):
    print("test_sendfile_keepalive:", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=10)

    s.sendall(f"GET /index.html HTTP/1.1\r\nHost: {host}\r\n\r\n".encode())
    hdr1, body1 = read_http_response(s)
    assert "200 OK" in hdr1 and len(body1) > 0

    s.sendall(f"GET /README HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr2, body2 = read_http_response(s)
    assert "200 OK" in hdr2 and len(body2) > 0

    extra = s.recv(10)
    assert extra == b""
    s.close()
    print("ok")

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20080)
    args = parser.parse_args()

    proc = start_qemu()
    try:
        test_sendfile_small(args.host, args.port)
        test_sendfile_1mb(args.host, args.port)
        test_sendfile_keepalive(args.host, args.port)
        print("all tests passed")
        return 0
    finally:
        stop_qemu(proc)

if __name__ == "__main__":
    sys.exit(main())
