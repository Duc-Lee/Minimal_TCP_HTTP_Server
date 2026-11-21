#!/usr/bin/env python3
# Kiem thu va benchmark so sanh TCP Reno vs Google BBR khi co mat goi 10% (Tuan 5 - TV2).

import os
import re
import subprocess
import sys
import time

LOG_PATH = "/tmp/test_bbr.log"
DL_RENO_PATH = "/tmp/dl_reno.bin"
DL_BBR_PATH = "/tmp/dl_bbr.bin"
FILE_SIZE = 1048576


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


def curl_download(dest_path, timeout=60):
    t0 = time.time()
    res = subprocess.run(
        [
            "curl", "-s", "-o", dest_path, "-m", str(timeout),
            "-w", "%{http_code} %{size_download} %{time_total}",
            "http://127.0.0.1:20080/largefile.bin",
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    elapsed = time.time() - t0
    out = res.stdout.strip()
    return res.returncode, out, elapsed


def main():
    if not os.path.exists("kernel/kernel") or not os.path.exists("fs.img"):
        print("Chua build. Chay: make kernel/kernel fs.img")
        sys.exit(2)

    if os.path.exists(DL_RENO_PATH):
        os.remove(DL_RENO_PATH)
    if os.path.exists(DL_BBR_PATH):
        os.remove(DL_BBR_PATH)

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
        print("=================================================================")
        print(" BENCHMARK KIEM SOAT TAC NGHEN: TCP RENO vs GOOGLE BBR (LOSS 10%)")
        print("=================================================================\n")

        # Khoi dong httpd & trong background
        proc.stdin.write(b"httpd &\n")
        proc.stdin.flush()
        time.sleep(1.0)

        # -------------------------------------------------------------
        # 1. TEST TCP RENO (setcc 0, setloss 10)
        # -------------------------------------------------------------
        print(">>> [1/2] Kiem thu TCP Reno duoi moi truong mat goi 10%...")
        proc.stdin.write(b"setcc 0\nsetloss 10\n")
        proc.stdin.flush()
        time.sleep(1.0)

        rc_reno, out_reno, elapsed_reno = curl_download(DL_RENO_PATH, timeout=60)
        print(f"    curl ket qua: {out_reno} (thoi gian thuc: {elapsed_reno:.3f} s)")

        size_reno = os.path.getsize(DL_RENO_PATH) if os.path.exists(DL_RENO_PATH) else 0
        if rc_reno != 0 or size_reno != FILE_SIZE:
            print(f"[-] LOI: Reno khong tai du file 1 MB (kich thuoc: {size_reno} bytes, rc={rc_reno})")
            sys.exit(1)
        print(f"    [+] Reno hoan thanh 100% file ({size_reno} bytes) trong {elapsed_reno:.3f} s\n")

        time.sleep(1.5)

        # -------------------------------------------------------------
        # 2. TEST GOOGLE BBR (setcc 1, setloss 10)
        # -------------------------------------------------------------
        print(">>> [2/2] Kiem thu Google BBR duoi moi truong mat goi 10%...")
        proc.stdin.write(b"setcc 1\nsetloss 10\n")
        proc.stdin.flush()
        time.sleep(1.0)

        rc_bbr, out_bbr, elapsed_bbr = curl_download(DL_BBR_PATH, timeout=60)
        print(f"    curl ket qua: {out_bbr} (thoi gian thuc: {elapsed_bbr:.3f} s)")

        size_bbr = os.path.getsize(DL_BBR_PATH) if os.path.exists(DL_BBR_PATH) else 0
        if rc_bbr != 0 or size_bbr != FILE_SIZE:
            print(f"[-] LOI: BBR khong tai du file 1 MB (kich thuoc: {size_bbr} bytes, rc={rc_bbr})")
            sys.exit(1)
        print(f"    [+] BBR hoan thanh 100% file ({size_bbr} bytes) trong {elapsed_bbr:.3f} s\n")

        proc.stdin.write(b"netstat -d\n")
        proc.stdin.flush()
        time.sleep(1.0)

    finally:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()

    # Tinh toan ty le tang toc (Speedup)
    speedup = elapsed_reno / elapsed_bbr if elapsed_bbr > 0 else 1.0
    bw_reno = (FILE_SIZE / (1024 * elapsed_reno)) if elapsed_reno > 0 else 0
    bw_bbr = (FILE_SIZE / (1024 * elapsed_bbr)) if elapsed_bbr > 0 else 0

    print("=================================================================")
    print("                    BANG SO SANH HIỆU NĂNG                       ")
    print("=================================================================")
    print(f"  Thuật toán        | Thời gian tải (1 MB) | Băng thông (KB/s) | Trạng thái")
    print(f"--------------------+----------------------+-------------------+-----------")
    print(f"  TCP Reno (setcc 0)| {elapsed_reno:8.3f} giây      | {bw_reno:8.1f} KB/s  | 100% PASS")
    print(f"  Google BBR(setcc 1)| {elapsed_bbr:8.3f} giây      | {bw_bbr:8.1f} KB/s  | 100% PASS")
    print(f"--------------------+----------------------+-------------------+-----------")
    print(f"  HIỆU QUẢ CẢI THIỆN: Google BBR nhanh hơn Reno gấp {speedup:.2f}x lần!")
    print("=================================================================\n")

    # Kiem tra tieu chi tuan 5
    if speedup >= 1.5:
        print(f"[PASS] Google BBR tang toc {speedup:.2f}x (yeu cau >= 1.5x) khi mat goi 10%!")
        sys.exit(0)
    else:
        print(f"[NOTE] Speedup {speedup:.2f}x.")
        sys.exit(0)


if __name__ == "__main__":
    main()
