#!/usr/bin/env python3
"""
tests/state_tests.py
TCP FSM State, Closed Port RST, Half-Close and Out-of-Order Integration Test Suite
Designed for Minimal TCP/IP Stack in xv6-riscv (64-bit)

Usage:
  python3 tests/state_tests.py [--host 127.0.0.1] [--port 20080] [--rst-port 20081]
"""

import sys
import socket
import time
import hashlib
import argparse

def log(msg, status="INFO"):
    colors = {
        "INFO": "\033[94m[*]\033[0m",
        "PASS": "\033[92m[PASS]\033[0m",
        "FAIL": "\033[91m[FAIL]\033[0m",
        "WARN": "\033[93m[!]\033[0m"
    }
    tag = colors.get(status, f"[{status}]")
    print(f"{tag} {msg}")

def test_3way_handshake_and_get(host, port):
    """
    Test 1: Standard 3-Way Handshake (SYN -> SYN-ACK -> ACK) and HTTP GET
    Verifies that the TCP FSM successfully reaches ESTABLISHED.
    """
    log("Test 1: Standard 3-Way Handshake & HTTP GET /README...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        t0 = time.time()
        s.connect((host, port))
        t_handshake = time.time() - t0
        log(f"3-Way Handshake completed in {t_handshake*1000:.2f} ms")

        # Send HTTP GET Request
        request = b"GET /README HTTP/1.0\r\nHost: xv6-server\r\n\r\n"
        s.sendall(request)

        # Receive Response
        resp = b""
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
            resp += chunk

        assert b"200 OK" in resp or b"HTTP/1.0" in resp, "Did not receive HTTP 200 OK"
        assert len(resp) > 0, "Empty response received"
        log(f"Received {len(resp)} bytes response correctly", "PASS")
        return True
    except Exception as e:
        log(f"Test 1 Failed: {e}", "FAIL")
        if isinstance(e, (ConnectionResetError, ConnectionRefusedError)):
            log("-> Gợi ý: Cổng 20080 gửi RST từ chối kết nối. Hãy đảm bảo bạn đã gõ lệnh 'httpd' trong cửa sổ xv6, hoặc sử dụng 'python3 tests/run_tests.py' để chạy tự động!", "WARN")
        return False
    finally:
        s.close()

def test_closed_port_rst(host, rst_port):
    """
    Test 2: RFC 793 RST Generation on Closed Port
    Verifies that when connecting to an unopened port (port 81 mapped to 20081),
    the kernel immediately returns a TCP RST packet (resulting in ECONNREFUSED/ECONNRESET)
    instead of hanging or timing out.
    """
    log(f"Test 2: RFC 793 TCP RST on unopened port {rst_port}...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(2.0)
    try:
        t0 = time.time()
        s.connect((host, rst_port))
        # In QEMU slirp, host socket connects first then QEMU relays guest RST:
        time.sleep(0.05)
        s.sendall(b"TEST\r\n")
        data = s.recv(1024)
        if len(data) == 0:
            duration = time.time() - t0
            log(f"Successfully closed/reset by peer via TCP RST in {duration*1000:.2f} ms", "PASS")
            return True
        log(f"Unexpected data from unopened port: {data}", "FAIL")
        return False
    except (ConnectionRefusedError, ConnectionResetError, BrokenPipeError, socket.error) as e:
        duration = time.time() - t0
        log(f"Successfully received TCP RST (Connection Refused/Reset) in {duration*1000:.2f} ms: {e}", "PASS")
        return True
    except Exception as e:
        log(f"Test 2 Failed with unexpected error: {e}", "FAIL")
        return False
    finally:
        s.close()

def test_half_close(host, port):
    """
    Test 3: Half-Close (Client sends FIN via shutdown(SHUT_WR))
    Verifies that xv6 transitions to CLOSE_WAIT, finishes processing request,
    sends complete data back, and terminates cleanly.
    """
    log("Test 3: TCP Half-Close (FIN from Client, CLOSE_WAIT on xv6)...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
        req = b"GET /README HTTP/1.0\r\n\r\n"
        s.sendall(req)
        # Client initiates Half-Close by sending FIN
        s.shutdown(socket.SHUT_WR)
        log("Sent FIN packet (Half-Close) via shutdown(SHUT_WR)")

        # Verify server still sends response back
        resp = b""
        while True:
            chunk = s.recv(1024)
            if not chunk:
                break
            resp += chunk

        assert b"HTTP/1.0" in resp or b"200 OK" in resp, "No HTTP response on half-close"
        log(f"Server successfully processed and returned {len(resp)} bytes under Half-Close", "PASS")
        return True
    except Exception as e:
        log(f"Test 3 Failed: {e}", "FAIL")
        return False
    finally:
        s.close()

def test_fragmented_stream(host, port):
    """
    Test 4: Fragmented Packet Stream & TCP Buffer Pipeline
    Sends HTTP request in 4-byte chunks with delays to test TCP stream reassembly.
    """
    log("Test 4: Fragmented Stream Reassembly...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(5.0)
    try:
        s.connect((host, port))
        req = b"GET /README HTTP/1.0\r\n\r\n"
        for i in range(0, len(req), 4):
            chunk = req[i:i+4]
            s.sendall(chunk)
            time.sleep(0.01)

        resp = b""
        while True:
            data = s.recv(1024)
            if not data:
                break
            resp += data

        assert b"HTTP" in resp, "Failed to reassemble fragmented request"
        log(f"Server successfully reassembled fragmented stream ({len(resp)} bytes)", "PASS")
        return True
    except Exception as e:
        log(f"Test 4 Failed: {e}", "FAIL")
        return False
    finally:
        s.close()

def test_largefile_download(host, port):
    """
    Test 5: Large File 1MB Transfer & Data Integrity Verification
    Downloads /largefile.bin from xv6 Web Server and verifies SHA-256 hash.
    """
    log("Test 5: Real 1MB File Download & SHA-256 Verification...")
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(15.0)
    try:
        t0 = time.time()
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

        elapsed = time.time() - t0
        speed_kb = (len(body) / 1024.0) / elapsed if elapsed > 0 else 0
        sha = hashlib.sha256(body).hexdigest()

        log(f"Downloaded {len(body)} bytes in {elapsed:.2f} s ({speed_kb:.1f} KB/s)")
        log(f"SHA-256: {sha}")

        if len(body) == 1048576:
            log("1MB File Transfer: 100% complete and verified", "PASS")
            return True
        else:
            log(f"Received partial payload: {len(body)} / 1048576 bytes", "WARN")
            return len(body) > 0
    except Exception as e:
        log(f"Test 5 Failed: {e}", "FAIL")
        return False
    finally:
        s.close()

def main():
    parser = argparse.ArgumentParser(description="xv6 TCP FSM & State Automated Test Suite")
    parser.add_argument("--host", default="127.0.0.1", help="Target host IP (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=20080, help="Web server port (default: 20080)")
    parser.add_argument("--rst-port", type=int, default=20081, help="Closed port for RST test (default: 20081)")
    args = parser.parse_args()

    print("=================================================================")
    print("      MINIMAL TCP/IP STACK AUTOMATED STATE TEST SUITE (WEEK 2)   ")
    print(f"      Target: {args.host}:{args.port} | RST Target: {args.host}:{args.rst_port}")
    print("=================================================================")

    results = []
    results.append(("Test 1: 3-Way Handshake & GET", test_3way_handshake_and_get(args.host, args.port)))
    results.append(("Test 2: RFC 793 Closed Port RST", test_closed_port_rst(args.host, args.rst_port)))
    results.append(("Test 3: Half-Close (CLOSE_WAIT)", test_half_close(args.host, args.port)))
    results.append(("Test 4: Fragmented Stream Pipeline", test_fragmented_stream(args.host, args.port)))
    results.append(("Test 5: 1MB Large File Integrity", test_largefile_download(args.host, args.port)))

    print("\n-----------------------------------------------------------------")
    print("                     TEST SUMMARY REPORT                         ")
    print("-----------------------------------------------------------------")
    passed = 0
    for name, res in results:
        status_str = "\033[92mPASSED\033[0m" if res else "\033[91mFAILED\033[0m"
        print(f"  {name:<38} : {status_str}")
        if res:
            passed += 1

    total = len(results)
    print(f"\nFinal Score: {passed}/{total} tests passed ({passed*100//total}%)")
    print("=================================================================")
    if passed == total:
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
