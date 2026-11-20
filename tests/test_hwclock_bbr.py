#!/usr/bin/env python3
# Kiem thu bo dem phan cung RISC-V (rdcycle, r_time) va ha tang do RTT /
# min_rtt 10s / delivery_rate do phan giai microsecond (Tuan 5 - TV1).

import os
import re
import subprocess
import sys
import time

LOG_PATH = "/tmp/test_hwclock_bbr.log"
LARGE_SIZE = 1048576


def qemu_cmd():
    return [
        "qemu-system-riscv64", "-machine", "virt", "-bios", "none",
        "-kernel", "kernel/kernel", "-m", "128M", "-smp", "3",
        "-nographic",
        "-global", "virtio-mmio.force-legacy=false",
        "-drive", "file=fs.img,if=none,format=raw,id=x0",
        "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
        "-netdev", "user,id=net0,hostfwd=tcp::20080-:80",
        "-device", "e1000,netdev=net0,bus=pcie.0",
    ]


def read_log():
    with open(LOG_PATH, "r", errors="replace") as f:
        return f.read()


def main():
    if not os.path.exists("kernel/kernel") or not os.path.exists("fs.img"):
        print("Chua build. Chay: make kernel/kernel fs.img")
        sys.exit(2)

    log = open(LOG_PATH, "w")
    proc = subprocess.Popen(
        qemu_cmd(), stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT
    )
    time.sleep(2.5)
    if proc.poll() is not None:
        log.flush()
        print("QEMU khong khoi dong duoc:")
        print(read_log())
        sys.exit(1)

    try:
        print("1. Chay cong cu hwclock trong xv6 (kiem tra rdcycle, r_time, hieu chuan, don dieu)...")
        proc.stdin.write(b"hwclock\n")
        proc.stdin.flush()
        time.sleep(4.5)

        print("2. Khoi dong httpd & (background) va tai largefile.bin (1 MB) kem do rtt_us / min_rtt_us / delivery_rate...")
        proc.stdin.write(b"httpd &\n")
        proc.stdin.flush()
        time.sleep(1.5)

        curl_proc = subprocess.Popen(
            [
                "curl", "-s", "-o", "/dev/null", "-m", "30",
                "-w", "%{http_code} %{size_download} %{time_total}",
                "http://127.0.0.1:20080/largefile.bin",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
        )

        time.sleep(0.15)
        proc.stdin.write(b"hwclock rtt 3\nnetstat -d\n")
        proc.stdin.flush()

        out, _ = curl_proc.communicate(timeout=35)
        print("   curl largefile.bin -> %s" % out.strip())
        time.sleep(2.0)
        proc.stdin.write(b"hwclock rtt 1\nnetstat -d\n")
        proc.stdin.flush()
        time.sleep(1.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    text = read_log()
    print("\n=== LOG TRICH XUAT TU CONSOLE XV6 ===")
    for line in text.splitlines():
        if any(
            k in line
            for k in [
                "hwclock:",
                "tcp_timer: selftest",
                "== hwclock",
                "timebase",
                "time hien tai",
                "cycle hien tai",
                "instret",
                "== hieu chuan",
                "xv6 ticks",
                "dong ho time",
                "ty le",
                "== do phan giai",
                "== kiem tra don dieu",
                "time  lui:",
                "cycle lui:",
                "== benchmark memmove",
                "bang thong copy",
                "rtt_us=",
                "tcp[",
            ]
        ):
            print("  " + line)

    failures = []
    if "hwclock: timebase=10000000 Hz" not in text:
        failures.append("thieu log khoi tao hwclock_init() luc boot")
    if "tcp_timer: selftest 10s min_rtt window & delivery_rate OK" not in text:
        failures.append("selftest 10s min_rtt window that bai")
    if "time  lui: 0 lan -> OK" not in text:
        failures.append("dong ho time khong don dieu")
    if "cycle lui: 0 lan -> OK" not in text:
        failures.append("bo dem rdcycle khong don dieu")

    rtts = [
        (int(m.group(1)), int(m.group(2)), int(m.group(3)))
        for m in re.finditer(
            r"rtt_us=(\d+)\s+min_rtt_us=(\d+).*?rate=(\d+)", text
        )
    ]
    valid_rtts = [(r, mr, bw) for (r, mr, bw) in rtts if r > 0 and mr > 0]
    if not valid_rtts:
        failures.append("khong ghi nhan duoc mau rtt_us / min_rtt_us > 0 khi tai 1 MB")
    else:
        for r, mr, bw in valid_rtts:
            if mr > r:
                failures.append("min_rtt_us (%d) > rtt_us (%d)" % (mr, r))

    fields = out.strip().split()
    if len(fields) < 2 or fields[0] != "200" or fields[1] != str(LARGE_SIZE):
        failures.append("tai largefile.bin that bai: %s" % out.strip())

    print("\n=== KET QUA KIEM THU TUAN 5 (TV1) ===")
    if failures:
        print("FAIL: " + "; ".join(failures))
        sys.exit(1)
    print("PASS: Tat ca kiem thu rdcycle, r_time, 10s min_rtt window va delivery_rate thanh cong!")


if __name__ == "__main__":
    main()
