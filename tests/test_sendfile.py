#!/usr/bin/env python3
# kiem thu zero-copy sendfile va truyen file tren xv6 (tuan 5 - tv3)

import hashlib
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

def read_full_response(s, expected_bytes=0):
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

    if expected_bytes > 0:
        content_len = expected_bytes

    body = bytearray(body_part)
    while len(body) < content_len:
        chunk = s.recv(min(8192, content_len - len(body)))
        if not chunk:
            break
        body.extend(chunk)

    return hdr_text, bytes(body)

def test_sendfile_small():
    print("test_sendfile_small:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(b"GET /README HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr, body = read_full_response(s)
    s.close()
    assert "200 OK" in hdr
    assert len(body) > 0
    print("ok")

def test_sendfile_1mb():
    print("test_sendfile_1mb:", end=" ", flush=True)
    t0 = time.time()
    s = socket.create_connection((HOST, PORT), timeout=15)
    s.sendall(b"GET /largefile.bin HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr, body = read_full_response(s, expected_bytes=1048576)
    s.close()
    t_elapsed = time.time() - t0

    assert "200 OK" in hdr
    assert len(body) == 1048576, f"Expected 1048576 bytes, got {len(body)}"

    # kiem tra ma bam sha-256 voi file goc tren dia
    with open("user/largefile.bin", "rb") as f:
        expected_sha = hashlib.sha256(f.read()).hexdigest()
    actual_sha = hashlib.sha256(body).hexdigest()
    assert actual_sha == expected_sha, f"SHA mismatch: {actual_sha} vs {expected_sha}"
    print(f"ok ({t_elapsed:.2f}s)")

def test_sendfile_keepalive():
    print("test_sendfile_keepalive:", end=" ", flush=True)
    s = socket.create_connection((HOST, PORT), timeout=10)

    # request 1: index.html
    s.sendall(b"GET /index.html HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
    hdr1, body1 = read_full_response(s)
    assert "200 OK" in hdr1 and len(body1) > 0

    # request 2: README
    s.sendall(b"GET /README HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n")
    hdr2, body2 = read_full_response(s)
    assert "200 OK" in hdr2 and len(body2) > 0

    extra = s.recv(10)
    assert extra == b""
    s.close()
    print("ok")

def main():
    proc = None
    try:
        s = socket.create_connection((HOST, PORT), timeout=0.5)
        s.close()
    except Exception:
        proc = start_qemu()

    try:
        test_sendfile_small()
        test_sendfile_1mb()
        test_sendfile_keepalive()
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
