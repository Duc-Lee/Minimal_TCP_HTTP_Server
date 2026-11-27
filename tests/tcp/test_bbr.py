
import argparse
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

def curl_download(url, dest_path, timeout=60):
    t0 = time.time()
    res = subprocess.run(
        [
            "curl", "-s", "-o", dest_path, "-m", str(timeout),
            "-w", "%{http_code} %{size_download} %{time_total}",
            url,
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    elapsed = time.time() - t0
    return res.returncode, res.stdout.strip(), elapsed

def run_trials(proc, url, cc_code, dest_path, trials=3):
    times = []
    proc.stdin.write(f"setcc {cc_code}\nsetloss 10\n".encode())
    proc.stdin.flush()
    time.sleep(0.8)

    for _ in range(trials):
        if os.path.exists(dest_path):
            os.remove(dest_path)
        rc, _, elapsed = curl_download(url, dest_path)
        size = os.path.getsize(dest_path) if os.path.exists(dest_path) else 0
        if rc != 0 or size != FILE_SIZE:
            return None, size
        times.append(elapsed)
        time.sleep(0.6)
    return times, FILE_SIZE


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=20080)
    args = parser.parse_args()

    url = f"http://{args.host}:{args.port}/largefile.bin"

    if os.path.exists(DL_RENO_PATH):
        os.remove(DL_RENO_PATH)
    if os.path.exists(DL_BBR_PATH):
        os.remove(DL_BBR_PATH)

    proc = start_qemu(
        boot_cmd="httpd &\n",
        log_file=LOG_PATH,
        port=args.port,
        rst_port=args.port + 1,
    )
    try:
        # 1. test reno (3 lan lay trung binh cong o muc mat goi 10%)
        print("test reno (loss 10%):")
        reno_times, reno_sz = run_trials(proc, url, 0, DL_RENO_PATH, trials=3)
        if not reno_times:
            print(f"fail: reno khong tai du file ({reno_sz} bytes)")
            return 1
        elapsed_reno = sum(reno_times) / len(reno_times)
        bw_reno = (FILE_SIZE / (1024 * elapsed_reno)) if elapsed_reno > 0 else 0
        samples_r = ", ".join(f"{t:.2f}s" for t in reno_times)
        print(f"  reno avg: {elapsed_reno:.2f}s, {bw_reno:.1f} KB/s (runs: {samples_r})")

        time.sleep(1.0)

        # 2. test bbr (3 lan lay trung binh cong o muc mat goi 10%)
        print("test bbr (loss 10%):")
        bbr_times, bbr_sz = run_trials(proc, url, 1, DL_BBR_PATH, trials=3)
        if not bbr_times:
            print(f"fail: bbr khong tai du file ({bbr_sz} bytes)")
            return 1
        elapsed_bbr = sum(bbr_times) / len(bbr_times)
        bw_bbr = (FILE_SIZE / (1024 * elapsed_bbr)) if elapsed_bbr > 0 else 0
        samples_b = ", ".join(f"{t:.2f}s" for t in bbr_times)
        print(f"  bbr avg:  {elapsed_bbr:.2f}s, {bw_bbr:.1f} KB/s (runs: {samples_b})")

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
