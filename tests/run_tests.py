#!/usr/bin/env python3
"""
tests/run_tests.py
End-to-End Test Runner: Boots QEMU, Launches httpd on xv6, Runs state_tests.py, and Terminates QEMU
"""

import subprocess
import time
import sys
import os

def main():
    print("[*] Launching QEMU with Minimal TCP/IP Stack...")
    cmd = ["qemu-system-riscv64",
           "-machine", "virt",
           "-bios", "none",
           "-kernel", "kernel/kernel",
           "-m", "128M",
           "-smp", "3",
           "-nographic",
           "-global", "virtio-mmio.force-legacy=false",
           "-drive", "file=fs.img,if=none,format=raw,id=x0",
           "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
           "-netdev", "user,id=net0,hostfwd=tcp::20080-:80,hostfwd=tcp::20081-:81",
           "-device", "e1000,netdev=net0,bus=pcie.0"]

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(2.5) # Wait for xv6 boot

    # Start httpd in xv6 shell
    print("[*] Sending 'httpd' command to xv6 shell...")
    proc.stdin.write(b"httpd\n")
    proc.stdin.flush()
    time.sleep(1.5)

    print("[*] Running automated test suite (tests/state_tests.py)...")
    res = subprocess.run([sys.executable, "tests/state_tests.py", "--host", "127.0.0.1", "--port", "20080", "--rst-port", "20081"])

    print("[*] Shutting down QEMU...")
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
