#!/usr/bin/env python3
# kiem tra tai song song va pool mbuf

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from common.qemu import start_qemu, stop_qemu

LOG_PATH = "/tmp/concurrent_stress.log"
LARGE_SIZE = 1048576

def run_round(host, port, round_no, conns, timeout, readme_size):
    procs = []
    for i in range(conns):
        if i % 2 == 0:
            path, expect = "largefile.bin", LARGE_SIZE
        else:
            path, expect = "README", readme_size
        p = subprocess.Popen(
            ["curl", "-s", "-o", "/dev/null", "-m", str(timeout),
             "-w", "%{http_code} %{size_download} %{time_total}",
             f"http://{host}:{port}/{path}"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        procs.append((p, path, expect))

    ok = 0
    for idx, (p, path, expect) in enumerate(procs):
        out, _ = p.communicate()
        fields = out.strip().split()
        good = (len(fields) >= 2 and fields[0] == "200" and fields[1] == str(expect))
        ok += good
        print(f"  conn {idx + 1} {path} -> {out.strip() or 'timeout'} ({'ok' if good else 'fail'})", flush=True)
    return ok

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=20080)
    ap.add_argument("--conns", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--loss", type=int, default=0)
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--settle", type=float, default=4.0)
    args = ap.parse_args()

    readme_size = os.path.getsize("README")
    proc = start_qemu(boot_cmd=f"setloss {args.loss}\nhttpd\n", log_file=LOG_PATH)

    total = args.conns * args.rounds
    ok = 0
    t0 = time.time()
    for r in range(1, args.rounds + 1):
        print(f"dot {r}: {args.conns} ket noi (loss {args.loss}%)", flush=True)
        ok += run_round(args.host, args.port, r, args.conns, args.timeout, readme_size)
    elapsed = time.time() - t0

    time.sleep(args.settle)
    with open(LOG_PATH, "r", errors="replace") as f:
        audit_pos = len(f.read())

    subprocess.run(
        ["curl", "-s", "-o", "/dev/null", "-m", "10", f"http://{args.host}:{args.port}/README"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)

    with open(LOG_PATH, "r", errors="replace") as f:
        text = f.read()
    stop_qemu(proc)

    failures = []
    print("ket qua:")
    print("request thanh cong: %d/%d (%.1f s)" % (ok, total, elapsed))
    if ok != total:
        failures.append("co request fail")

    if re.search(r"panic", text):
        failures.append("kernel panic")
    if "[mbuf] BUG double free" in text:
        failures.append("double free mbuf")

    m_ring = re.search(r"e1000: rx_ring=(\d+) tx_ring=(\d+)", text)
    idle_all = re.findall(r"\[mbuf\] idle: free=(\d+)/(\d+), burst_used=(\d+)", text)
    idle_after = re.findall(r"\[mbuf\] idle: free=(\d+)/(\d+), burst_used=(\d+)", text[audit_pos:])
    if not m_ring:
        failures.append("khong thay rx_ring trong log")
    elif not idle_after:
        failures.append("pool mbuf khong ve idle sau dot cuoi")
    else:
        rx_ring = int(m_ring.group(1))
        free, nmbuf, _ = map(int, idle_after[-1])
        max_used = max(int(u) for _, _, u in idle_all)
        expect = nmbuf - rx_ring
        print(f"pool mbuf: free={free}/{nmbuf} (ky vong {expect}), max_burst={max_used}")
        if free != expect:
            failures.append("ro ri %d mbuf" % (expect - free))

    min_frees = [int(x) for x in re.findall(r"min_free=(\d+)", text)]
    if min_frees:
        print("min_free thap nhat: %d" % min(min_frees))
    for label, pat in [("low watermark", r"\[mbuf\] WARN low watermark"),
                       ("recovered", r"\[mbuf\] recovered"),
                       ("pool empty", r"\[mbuf\] WARN pool empty"),
                       ("rx drop", r"\[e1000\] WARN rx drop"),
                       ("rx overrun", r"\[e1000\] WARN rx overrun"),
                       ("tx ring full", r"\[e1000\] WARN tx ring full")]:
        n = len(re.findall(pat, text))
        if n > 0:
            print(f"  {label}: {n}")

    if failures:
        print("fail: " + "; ".join(failures))
        return 1
    print("pass")
    return 0

if __name__ == "__main__":
    sys.exit(main())
