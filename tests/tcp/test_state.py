#!/usr/bin/env python3
# kiem tra may trang thai tcp fsm

import argparse
import os
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu

def test_handshake_and_get(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
        s.sendall(b"GET /README HTTP/1.0\r\nHost: xv6-server\r\n\r\n")
        resp = b""
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
            resp += chunk
        return b"200 OK" in resp or b"HTTP/1.0" in resp
    except Exception:
        return False
    finally:
        s.close()

def test_closed_port_rst(host, rst_port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2.0)
    try:
        s.connect((host, rst_port))
        time.sleep(0.05)
        s.sendall(b"TEST\r\n")
        data = s.recv(1024)
        return len(data) == 0
    except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError, socket.error):
        return True
    except Exception:
        return False
    finally:
        s.close()

def test_half_close(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
        s.sendall(b"GET /README HTTP/1.0\r\n\r\n")
        s.shutdown(socket.SHUT_WR)
        resp = b""
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
            resp += chunk
        return b"HTTP/1.0" in resp or b"200 OK" in resp
    except Exception:
        return False
    finally:
        s.close()

def test_fragmented_stream(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
        req = b"GET /README HTTP/1.0\r\n\r\n"
        for i in range(0, len(req), 4):
            s.sendall(req[i:i+4])
            time.sleep(0.01)
        resp = b""
        while True:
            data = s.recv(1024)
            if not data:
                break
            resp += data
        return b"HTTP" in resp
    except Exception:
        return False
    finally:
        s.close()

def test_largefile_download(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(15.0)
    try:
        s.connect((host, port))
        s.sendall(b"GET /largefile.bin HTTP/1.0\r\n\r\n")
        body = b""
        header_done = False
        headers = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            if not header_done:
                headers += chunk
                delim = headers.find(b"\r\n\r\n")
                if delim != -1:
                    header_done = True
                    body = headers[delim+4:]
            else:
                body += chunk
        return len(body) == 1048576
    except Exception:
        return False
    finally:
        s.close()

def test_client_abort_and_reclaim(host, port):
    s_next = None
    try:
        s_rst = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_rst.settimeout(3.0)
        s_rst.connect((host, port))
        s_rst.sendall(b"GET /")
        s_rst.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
        s_rst.close()
        time.sleep(0.1)

        s_next = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s_next.settimeout(5.0)
        s_next.connect((host, port))
        s_next.sendall(b"GET /README HTTP/1.0\r\n\r\n")
        resp = b""
        while True:
            chunk = s_next.recv(1024)
            if not chunk:
                break
            resp += chunk
        return b"HTTP/1.0" in resp or b"200 OK" in resp
    except Exception:
        return False
    finally:
        if s_next:
            s_next.close()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20080)
    parser.add_argument("--rst-port", type=int, default=20081)
    args = parser.parse_args()

    proc = start_qemu("httpd\n")
    try:
        tests = [
            ("test_3way_handshake", lambda: test_handshake_and_get(args.host, args.port)),
            ("test_closed_port_rst", lambda: test_closed_port_rst(args.host, args.rst_port)),
            ("test_half_close", lambda: test_half_close(args.host, args.port)),
            ("test_fragmented_stream", lambda: test_fragmented_stream(args.host, args.port)),
            ("test_largefile_1mb", lambda: test_largefile_download(args.host, args.port)),
            ("test_client_abort", lambda: test_client_abort_and_reclaim(args.host, args.port)),
        ]

        failed = 0
        for name, fn in tests:
            print(f"{name}:", end=" ", flush=True)
            if fn():
                print("ok")
            else:
                print("fail")
                failed += 1

        if failed == 0:
            print("all tests passed")
            return 0
        else:
            print(f"{failed}/{len(tests)} tests failed")
            return 1
    finally:
        stop_qemu(proc)

if __name__ == "__main__":
    sys.exit(main())
