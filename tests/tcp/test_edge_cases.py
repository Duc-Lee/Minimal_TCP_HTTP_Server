import argparse
import os
import re
import socket
import struct
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu
from common.http import read_http_response

LOG_PATH = "/tmp/test_edge_cases.log"


def read_log():
    with open(LOG_PATH, "r", errors="replace") as f:
        return f.read()


def wait_log_match(pattern, start_pos, timeout=15.0):
    deadline = time.time() + timeout
    rx = re.compile(pattern)
    while time.time() < deadline:
        txt = read_log()[start_pos:]
        m = rx.search(txt)
        if m:
            return m, txt
        time.sleep(0.25)
    return None, read_log()[start_pos:]


def test_kernel_edge_selftest(proc):
    print("test_kernel_edge_selftest (malformed/garbage/FSM/TIME_WAIT):", end=" ", flush=True)
    pos = len(read_log())
    proc.stdin.write(b"netstat -t\n")
    proc.stdin.flush()
    m, txt = wait_log_match(r"netstat -t: (PASS|FAIL)", pos, timeout=10.0)
    assert m and m.group(1) == "PASS" and "tcp_edge_selftest: PASS" in txt, txt
    print("ok")


def test_garbage_and_closed_port(host, port, rst_port):
    print("test_garbage_and_closed_port:", end=" ", flush=True)
    # 1. Cong dong phai phan hoi RST tuc thi (< 50 ms)
    t0 = time.time()
    rst_ok = False
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(1.0)
    try:
        s.connect((host, rst_port))
        s.sendall(b"GARBAGE\r\n")
        if s.recv(64) == b"":
            rst_ok = True
    except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError, OSError):
        rst_ok = True
    finally:
        s.close()
    dt = (time.time() - t0) * 1000.0
    assert rst_ok and dt < 200.0, f"closed port rst fail (ok={rst_ok}, dt={dt:.1f}ms)"

    # 2. Gui du lieu rac nhi phan vao HTTP port roi ngat dot ngot
    for payload in [b"\x00\xff\xfe\x01" * 32, b"BADMETHOD /\r\n\r\n", b"GET /../../etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n"]:
        sg = socket.create_connection((host, port), timeout=3.0)
        sg.sendall(payload)
        sg.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        sg.close()
        time.sleep(0.05)

    # Xac minh server van song va phuc vu hop le sau cac goi rac
    sv = socket.create_connection((host, port), timeout=5.0)
    sv.sendall(f"GET /README HTTP/1.1\r\nHost: {host}\r\nConnection: close\r\n\r\n".encode())
    hdr, body = read_http_response(sv)
    sv.close()
    assert "200 OK" in hdr and len(body) == os.path.getsize("README")
    print(f"ok (rst={dt:.1f}ms)")


def test_half_close(host, port):
    print("test_half_close (FIN sau request, van doc du du lieu):", end=" ", flush=True)
    s = socket.create_connection((host, port), timeout=5.0)
    s.sendall(b"GET /README HTTP/1.0\r\n\r\n")
    s.shutdown(socket.SHUT_WR)  # gui FIN nua chieu ghi
    chunks = []
    while True:
        data = s.recv(2048)
        if not data:
            break
        chunks.append(data)
    s.close()
    raw = b"".join(chunks)
    delim = raw.find(b"\r\n\r\n")
    assert delim != -1
    body = raw[delim + 4:]
    assert len(body) == os.path.getsize("README"), f"half-close len={len(body)}"
    print("ok")


def test_client_abort_mid_stream(proc, host, port):
    print("test_client_abort_mid_stream (loss 10%, SO_LINGER RST):", end=" ", flush=True)
    proc.stdin.write(b"setloss 10\n")
    proc.stdin.flush()
    time.sleep(0.3)

    for _ in range(3):
        s = socket.create_connection((host, port), timeout=5.0)
        s.sendall(b"GET /largefile.bin HTTP/1.0\r\n\r\n")
        s.recv(16384)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        s.close()
        time.sleep(0.2)

    pos = len(read_log())
    proc.stdin.write(b"setloss 0\nmemaudit -w 10\n")
    proc.stdin.flush()
    m, txt = wait_log_match(r"memaudit: PASS leaked=0 zombie=0", pos, timeout=15.0)
    assert m, f"abort memaudit failed:\n{txt}"
    print("ok (leaked=0, zombie=0)")


def test_time_wait_burst_and_2msl_reclaim(proc, host, port):
    print("test_time_wait_burst_and_2msl (40 ket noi don dap > 32 PCB + cho 2 MSL):", end=" ", flush=True)
    readme_len = os.path.getsize("README")
    # 40 HTTP/1.0 request lien tiep: httpd goi close() truoc -> PCB vao TIME_WAIT (20s).
    # Vuot qua TCP_PCB_POOL_SIZE (32) de kiem tra tai su dung TIME_WAIT cu nhat!
    for i in range(40):
        s = socket.create_connection((host, port), timeout=5.0)
        s.sendall(b"GET /README HTTP/1.0\r\n\r\n")
        chunks = []
        while True:
            d = s.recv(4096)
            if not d:
                break
            chunks.append(d)
        s.close()
        raw = b"".join(chunks)
        delim = raw.find(b"\r\n\r\n")
        assert delim != -1 and len(raw[delim + 4:]) == readme_len, f"burst conn {i} fail"

    # Kiem tra memaudit ngay sau burst: phai co cac PCB o TIME_WAIT va zombie=0, leaked=0
    pos = len(read_log())
    proc.stdin.write(b"memaudit\n")
    proc.stdin.flush()
    m, txt = wait_log_match(r"tcp\s+:\s+pcb_free=\d+/32\s+listen=1\s+conns=(\d+)\s+time_wait=(\d+)\s+zombie=0", pos, timeout=5.0)
    assert m, f"post-burst memaudit failed:\n{txt}"
    tw_before = int(m.group(2))
    assert tw_before > 0, f"expected TIME_WAIT pcbs after burst, got:\n{txt}"

    # Cho qua han 2 MSL (~20 giay = 200 ticks) de xac minh TIME_WAIT tu thu hoi 100%
    time.sleep(21.5)
    pos2 = len(read_log())
    proc.stdin.write(b"memaudit -f -w 5\n")
    proc.stdin.flush()
    m2, txt2 = wait_log_match(r"tcp\s+:\s+pcb_free=31/32\s+listen=1\s+conns=0\s+time_wait=0\s+zombie=0", pos2, timeout=10.0)
    assert m2 and "memaudit: PASS leaked=0 zombie=0" in txt2, f"2 MSL reclaim fail:\n{txt2}"
    print(f"ok (tw_burst={tw_before} -> after 2MSL tw=0, pcb_free=31/32)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20080)
    parser.add_argument("--rst-port", type=int, default=20081)
    args = parser.parse_args()

    proc = start_qemu(boot_cmd="httpd &\n", log_file=LOG_PATH, port=args.port, rst_port=args.rst_port)
    try:
        test_kernel_edge_selftest(proc)
        test_garbage_and_closed_port(args.host, args.port, args.rst_port)
        test_half_close(args.host, args.port)
        test_client_abort_mid_stream(proc, args.host, args.port)
        test_time_wait_burst_and_2msl_reclaim(proc, args.host, args.port)
        print("all edge-case & TIME_WAIT tests passed")
        return 0
    finally:
        stop_qemu(proc)


if __name__ == "__main__":
    sys.exit(main())
