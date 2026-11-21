import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import Qemu

LOG_PATH = "/tmp/test_hwclock_bbr.log"
LARGE_SIZE = 1048576

def main():
    q = Qemu(log_path=LOG_PATH)
    q.start(boot_cmd=None)

    try:
        print("kiem tra hwclock:", flush=True)
        q.send("hwclock\n")
        time.sleep(4.5)

        print("kiem tra rtt va delivery_rate:", flush=True)
        q.send("httpd &\n")
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
        q.send("hwclock rtt 3\nnetstat -d\n")

        out, _ = curl_proc.communicate(timeout=35)
        print("  curl:", out.strip())
        time.sleep(2.0)
        q.send("hwclock rtt 1\nnetstat -d\n")
        time.sleep(1.0)
    finally:
        text = q.read_log()
        q.stop()

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

    if failures:
        print("fail: " + "; ".join(failures))
        return 1
    print("pass")
    return 0

if __name__ == "__main__":
    sys.exit(main())
