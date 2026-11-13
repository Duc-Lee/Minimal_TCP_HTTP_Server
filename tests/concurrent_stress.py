#!/usr/bin/env python3
# Kiem thu tai song song cho tang mang (tuan 4 - TV1).
#
# Khoi dong xv6 trong QEMU, chay httpd, ban nhieu curl DONG THOI vao server
# (mot nua tai largefile.bin 1 MB, mot nua tai README), lap lai nhieu dot.
# Sau do doc console kernel de kiem tra:
#   1. Moi request deu tra ve 200 va du so byte.
#   2. Kernel khong panic, khong co "[mbuf] BUG double free".
#   3. Sau dot tai cuoi, pool mbuf ve lai muc idle:
#        free == NMBUF - RX_RING_SIZE
#      (rx ring cua e1000 giu thuong truc RX_RING_SIZE mbuf). Neu thieu tuc la
#      con mbuf bi ro ri o retransmit_q / tx ring / arp pending.
#   4. In thong ke cac canh bao low watermark / rx drop / tx ring full.
#
# Cach dung:
#   python3 tests/concurrent_stress.py [--conns 5] [--rounds 3] [--loss 0]
#                                      [--timeout 60] [--settle 4]
# Luu y: mac dinh --conns=5 vi user/httpd.c (TV3) hien dat listen(listen_fd, 5)
# va kernel/tcp_input.c (TV2) bo qua ket noi vuot backlog_max. Khi TV2/TV3 hoan
# tat worker pool va backlog tuan 4, co the chay voi --conns 8 hoac cao hon.
# Can build truoc: make kernel/kernel fs.img
import argparse
import os
import re
import subprocess
import sys
import time

LOG_PATH = "/tmp/concurrent_stress.log"
LARGE_SIZE = 1048576


def qemu_cmd():
    return ["qemu-system-riscv64", "-machine", "virt", "-bios", "none",
            "-kernel", "kernel/kernel", "-m", "128M", "-smp", "3",
            "-nographic",
            "-global", "virtio-mmio.force-legacy=false",
            "-drive", "file=fs.img,if=none,format=raw,id=x0",
            "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
            "-netdev", "user,id=net0,hostfwd=tcp::20080-:80",
            "-device", "e1000,netdev=net0,bus=pcie.0"]


def read_log():
    with open(LOG_PATH, "r", errors="replace") as f:
        return f.read()


def run_round(round_no, conns, timeout, readme_size):
    procs = []
    for i in range(conns):
        if i % 2 == 0:
            path, expect = "largefile.bin", LARGE_SIZE
        else:
            path, expect = "README", readme_size
        p = subprocess.Popen(
            ["curl", "-s", "-o", "/dev/null", "-m", str(timeout),
             "-w", "%{http_code} %{size_download} %{time_total}",
             "http://127.0.0.1:20080/" + path],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        procs.append((p, path, expect))

    ok = 0
    for idx, (p, path, expect) in enumerate(procs):
        out, _ = p.communicate()
        fields = out.strip().split()
        good = (len(fields) >= 2 and fields[0] == "200"
                and fields[1] == str(expect))
        ok += good
        print("  dot %d / conn %d %-13s -> %s %s" %
              (round_no, idx + 1, path, out.strip() or "(timeout)",
               "OK" if good else "FAIL"), flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--conns", type=int, default=5)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--loss", type=int, default=0)
    ap.add_argument("--timeout", type=int, default=60)
    ap.add_argument("--settle", type=float, default=4.0,
                    help="so giay cho tcp/tx ring xa het sau dot cuoi")
    args = ap.parse_args()

    if not os.path.exists("kernel/kernel") or not os.path.exists("fs.img"):
        print("Chua build. Chay: make kernel/kernel fs.img")
        sys.exit(2)
    readme_size = os.path.getsize("README")

    log = open(LOG_PATH, "w")
    proc = subprocess.Popen(qemu_cmd(), stdin=subprocess.PIPE, stdout=log,
                            stderr=subprocess.STDOUT)
    time.sleep(2.5)
    if proc.poll() is not None:
        log.flush()
        print("QEMU khong khoi dong duoc (cong 20080 bi chiem?). Log:")
        print(read_log())
        sys.exit(1)

    proc.stdin.write(("setloss %d\nhttpd\n" % args.loss).encode())
    proc.stdin.flush()
    time.sleep(1.5)

    total = args.conns * args.rounds
    ok = 0
    last_round_log_pos = 0
    t0 = time.time()
    for r in range(1, args.rounds + 1):
        log.flush()
        last_round_log_pos = len(read_log())
        print("Dot %d: %d ket noi dong thoi (loss %d%%)" %
              (r, args.conns, args.loss), flush=True)
        ok += run_round(r, args.conns, args.timeout, readme_size)
    elapsed = time.time() - t0

    # cho retransmit_q / tx ring xa het, sau do gui 1 request nhe de kich hoat
    # bao cao "[mbuf] idle:" xac nhan pool ve dung NMBUF - RX_RING_SIZE.
    time.sleep(args.settle)
    log.flush()
    audit_log_pos = len(read_log())
    subprocess.run(
        ["curl", "-s", "-o", "/dev/null", "-m", "10",
         "http://127.0.0.1:20080/README"],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)
    log.flush()
    text = read_log()

    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    log.close()

    failures = []
    print()
    print("=== KET QUA ===")
    print("Request thanh cong: %d/%d (%.1f s)" % (ok, total, elapsed))
    if ok != total:
        failures.append("co request that bai")

    if re.search(r"panic", text):
        failures.append("kernel panic")
    if "[mbuf] BUG double free" in text:
        failures.append("phat hien double free mbuf")

    m_ring = re.search(r"e1000: rx_ring=(\d+) tx_ring=(\d+)", text)
    idle_all = re.findall(r"\[mbuf\] idle: free=(\d+)/(\d+), burst_used=(\d+)",
                          text)
    idle_after = re.findall(r"\[mbuf\] idle: free=(\d+)/(\d+), burst_used=(\d+)",
                            text[audit_log_pos:])
    if not m_ring:
        failures.append("khong thay dong 'e1000: rx_ring=' (kernel cu?)")
    elif not idle_after:
        failures.append("pool mbuf khong ve muc idle sau dot cuoi (nghi ro ri)")
    else:
        rx_ring = int(m_ring.group(1))
        free, nmbuf, _ = map(int, idle_after[-1])
        max_used = max(int(u) for _, _, u in idle_all)
        expect = nmbuf - rx_ring
        print("Pool mbuf sau dot cuoi: free=%d/%d (mong doi %d = NMBUF - "
              "rx_ring), max_burst_used=%d" % (free, nmbuf, expect, max_used))
        if free != expect:
            failures.append("mbuf_free_count = %d != %d -> ro ri %d mbuf" %
                            (free, expect, expect - free))

    min_frees = [int(x) for x in re.findall(r"min_free=(\d+)", text)]
    if min_frees:
        print("min_free thap nhat quan sat: %d" % min(min_frees))
    for label, pat in [("Low watermark", r"\[mbuf\] WARN low watermark"),
                       ("Recovered", r"\[mbuf\] recovered"),
                       ("Pool empty", r"\[mbuf\] WARN pool empty"),
                       ("RX drop (no mbuf)", r"\[e1000\] WARN rx drop"),
                       ("RX overrun", r"\[e1000\] WARN rx overrun"),
                       ("TX ring full", r"\[e1000\] WARN tx ring full")]:
        n = len(re.findall(pat, text))
        print("  %-18s: %d dong log" % (label, n))

    print("Log console day du: %s" % LOG_PATH)
    if failures:
        print("FAIL: " + "; ".join(failures))
        sys.exit(1)
    print("PASS")


if __name__ == "__main__":
    main()
