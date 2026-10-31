#!/usr/bin/env python3
import subprocess
import time
import sys

def main():
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

    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(2.5)

    proc.stdin.write(b"httpd\n")
    proc.stdin.flush()
    time.sleep(1.0)

    res = subprocess.run([sys.executable, "tests/state_tests.py", "--host", "127.0.0.1", "--port", "20080", "--rst-port", "20081"])

    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()

    sys.exit(res.returncode)

if __name__ == "__main__":
    main()
