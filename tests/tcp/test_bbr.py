#!/usr/bin/env python3
# so sanh hieu nang tcp reno vs bbr khi mat goi 10%

import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu

LOG_PATH = "/tmp/test_bbr.log"
DL_RENO_PATH = "/tmp/dl_reno.bin"
DL_BBR_PATH = "/tmp/dl_bbr.bin"
FILE_SIZE = 1048576

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
    return res.returncode, res.stdout.strip(), elapsed

def main():
    if os.path.exists(DL_RENO_PATH):
        os.remove(DL_RENO_PATH)
    if os.path.exists(DL_BBR_PATH):
        os.remove(DL_BBR_PATH)

    proc = start_qemu(boot_cmd="httpd &\n", log_file=LOG_PATH)
    try:
        # 1. test reno
        print("test reno (loss 10%):")
        proc.stdin.write(b"setcc 0\nsetloss 10\n")
        proc.stdin.flush()
        time.sleep(1.0)

        rc_reno, _, elapsed_reno = curl_download(DL_RENO_PATH)
        size_reno = os.path.getsize(DL_RENO_PATH) if os.path.exists(DL_RENO_PATH) else 0
        if rc_reno != 0 or size_reno != FILE_SIZE:
            print(f"fail: reno khong tai du file ({size_reno} bytes)")
            return 1
        bw_reno = (FILE_SIZE / (1024 * elapsed_reno)) if elapsed_reno > 0 else 0
        print(f"  reno: {elapsed_reno:.2f}s, {bw_reno:.1f} KB/s")

        time.sleep(1.5)

        # 2. test bbr
        print("test bbr (loss 10%):")
        proc.stdin.write(b"setcc 1\nsetloss 10\n")
        proc.stdin.flush()
        time.sleep(1.0)

        rc_bbr, _, elapsed_bbr = curl_download(DL_BBR_PATH)
        size_bbr = os.path.getsize(DL_BBR_PATH) if os.path.exists(DL_BBR_PATH) else 0
        if rc_bbr != 0 or size_bbr != FILE_SIZE:
            print(f"fail: bbr khong tai du file ({size_bbr} bytes)")
            return 1
        bw_bbr = (FILE_SIZE / (1024 * elapsed_bbr)) if elapsed_bbr > 0 else 0
        print(f"  bbr: {elapsed_bbr:.2f}s, {bw_bbr:.1f} KB/s")

        proc.stdin.write(b"netstat -d\n")
        proc.stdin.flush()
        time.sleep(1.0)
    finally:
        stop_qemu(proc)

    speedup = elapsed_reno / elapsed_bbr if elapsed_bbr > 0 else 1.0
    print(f"speedup: {speedup:.2f}x")
    if speedup < 1.5:
        print(f"fail: speedup {speedup:.2f}x < 1.5x")
        return 1
    print("pass")
    return 0

if __name__ == "__main__":
    sys.exit(main())
