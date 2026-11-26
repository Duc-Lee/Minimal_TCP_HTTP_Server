#!/usr/bin/env python3
# Kiem thu Tuan 6 - TV1: stress test mat goi khac nghiet + kiem toan zero-leak.
#
# Khoi dong xv6 trong QEMU, chay httpd va lan luot:
#   0. Kiem toan nen luc boot (memaudit) -> 512/512, leaked=0.
#   1. Mat goi 20%  : tai largefile.bin (1 MB) N lan, so khop SHA-256.
#   2. Mat goi 30%  : tai largefile.bin (1 MB) N lan, so khop SHA-256.
#   3. Mat goi 50%  : tai README / index.html (file nho) - chung minh he thong
#                     van tu phuc hoi, khong panic / deadlock.
#   4. Client abort : o 30% loss, kill curl giua chung khi dang tai 1 MB
#                     (tao RST/FIN bat thuong, pcb mo coi o FIN_WAIT/LAST_ACK).
#   5. Tai song song: nhieu curl dong thoi o 20% loss.
#   Sau moi pha: setloss 0, `memaudit -w` cho ngan xep ve trang thai nghi roi
#   chot bien ban: leaked=0, zombie=0, free + rx_ring == NMBUF.
#   6. Bien ban cuoi `memaudit -f -w` (xoa bang arp) + kiem tra console khong
#      co panic / double free, server van phuc vu binh thuong.
#
# Cach dung:
#   make kernel/kernel fs.img
#   python3 tests/test_week6_zero_leak.py [--rounds 3] [--conns 8] [--quick]
import argparse
import hashlib
import os
import re
import subprocess
import sys
import time

LOG_PATH = "/tmp/test_week6_zero_leak.log"
URL = "http://127.0.0.1:20080/"
LARGE = "largefile.bin"


def qemu_cmd():
    return ["qemu-system-riscv64", "-machine", "virt", "-bios", "none",
            "-kernel", "kernel/kernel", "-m", "128M", "-smp", "3",
            "-nographic",
            "-global", "virtio-mmio.force-legacy=false",
            "-drive", "file=fs.img,if=none,format=raw,id=x0",
            "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
            "-netdev", "user,id=net0,hostfwd=tcp::20080-:80",
            "-device", "e1000,netdev=net0,bus=pcie.0"]


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


class Xv6:
    def __init__(self):
        self.log = open(LOG_PATH, "w")
        self.proc = subprocess.Popen(qemu_cmd(), stdin=subprocess.PIPE,
                                     stdout=self.log,
                                     stderr=subprocess.STDOUT)

    def text(self):
        self.log.flush()
        with open(LOG_PATH, "r", errors="replace") as f:
            return f.read()

    def send(self, line):
        self.proc.stdin.write((line + "\n").encode())
        self.proc.stdin.flush()

    # gui lenh va cho den khi regex xuat hien trong phan log moi
    def run(self, line, until, timeout):
        start = len(self.text())
        self.send(line)
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.text()[start:]
            if re.search(until, chunk):
                time.sleep(0.2)
                return self.text()[start:]
            if self.proc.poll() is not None:
                break
            time.sleep(0.2)
        return self.text()[start:]

    def alive(self):
        return self.proc.poll() is None

    def close(self):
        self.proc.terminate()
        try:
            self.proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.proc.kill()
        self.log.close()


def parse_audit(out):
    # lay bien ban memaudit cuoi cung trong doan log
    res = {}
    pats = {
        "free": r"mbuf pool : total=(\d+) free=(\d+) \(walk=(\d+)\)",
        "e1000": r"e1000     : rx_ring=(\d+) tx_inflight=(\d+) loss=(\d+)% "
                 r"tx_pkts=(\d+) tx_loss_sim=(\d+)",
        "tcp": r"tcp       : pcb_free=(\d+)/(\d+) listen=(\d+) conns=(\d+) "
               r"time_wait=(\d+) zombie=(\d+) rtx_mbufs=(\d+) ooo_mbufs=(\d+)",
        "acct": r"doi chieu : .*= (\d+)/(\d+)",
        "min_free": r"min_free=(\d+)\s*\n",
    }
    for k, p in pats.items():
        ms = re.findall(p, out)
        if ms:
            res[k] = ms[-1]
    m = re.findall(r"memaudit: (PASS|FAIL) leaked=(-?\d+) zombie=(\d+)", out)
    if m:
        res["verdict"], res["leaked"], res["zombie"] = m[-1][0], int(
            m[-1][1]), int(m[-1][2])
    return res


def audit(xv, label, wait=60, flush=False):
    cmd = "memaudit %s-w %d" % ("-f " if flush else "", wait)
    out = xv.run(cmd, r"memaudit: (PASS|FAIL)", wait + 15)
    a = parse_audit(out)
    ok = a.get("verdict") == "PASS"
    free = a.get("free", ("?", "?", "?"))
    tcp = a.get("tcp", ["?"] * 8)
    e = a.get("e1000", ["?"] * 5)
    acct = a.get("acct", ("?", "?"))
    print("  [memaudit] %-26s free=%s/%s walk=%s rx_ring=%s accounted=%s/%s "
          "conns=%s tw=%s zombie=%s rtx=%s ooo=%s tx_loss_sim=%s -> %s" %
          (label, free[1], free[0], free[2], e[0], acct[0], acct[1], tcp[3],
           tcp[4], tcp[5], tcp[6], tcp[7], e[4],
           a.get("verdict", "KHONG CO KET QUA")), flush=True)
    return ok, a


def curl(path, timeout, out="/dev/null"):
    r = subprocess.run(
        ["curl", "-s", "-o", out, "-m", str(timeout), "-w",
         "%{http_code} %{size_download} %{time_total}", URL + path],
        capture_output=True, text=True)
    f = r.stdout.strip().split()
    code = f[0] if f else "000"
    size = int(f[1]) if len(f) > 1 else 0
    t = float(f[2]) if len(f) > 2 else 0.0
    return code, size, t


def phase_large(xv, loss, rounds, timeout, ref_sha, results):
    xv.run("setloss %d" % loss, r"set loss rate", 5)
    print("Pha mat goi %d%%: tai %s (1 MB) x%d" % (loss, LARGE, rounds),
          flush=True)
    ok = 0
    times = []
    for i in range(rounds):
        dl = "/tmp/w6_dl_%d.bin" % loss
        if os.path.exists(dl):
            os.remove(dl)
        code, size, t = curl(LARGE, timeout, dl)
        good = (code == "200" and size == 1048576 and os.path.exists(dl)
                and sha256_file(dl) == ref_sha)
        ok += good
        if good:
            times.append(t)
        print("  lan %d: http=%s size=%d time=%.2fs sha256=%s" %
              (i + 1, code, size, t, "KHOP" if good else "SAI/THIEU"),
              flush=True)
    results.append(("loss %d%% 1MB" % loss, ok, rounds, times))
    xv.run("setloss 0", r"set loss rate", 5)
    return ok == rounds


def phase_small(xv, loss, rounds, timeout, results):
    xv.run("setloss %d" % loss, r"set loss rate", 5)
    readme = os.path.getsize("README")
    print("Pha mat goi %d%%: tai README (%d B) x%d" % (loss, readme, rounds),
          flush=True)
    ok = 0
    times = []
    for i in range(rounds):
        code, size, t = curl("README", timeout)
        good = code == "200" and size == readme
        ok += good
        if good:
            times.append(t)
        print("  lan %d: http=%s size=%d time=%.2fs %s" %
              (i + 1, code, size, t, "OK" if good else "THAT BAI"),
              flush=True)
    results.append(("loss %d%% README" % loss, ok, rounds, times))
    xv.run("setloss 0", r"set loss rate", 5)
    # 50% la de chung minh tu phuc hoi, khong bat buoc 100% request thanh cong
    return ok > 0


def phase_abort(xv, loss, rounds, results):
    xv.run("setloss %d" % loss, r"set loss rate", 5)
    print("Pha client abort (loss %d%%): kill curl giua chung x%d" %
          (loss, rounds), flush=True)
    for i in range(rounds):
        p = subprocess.Popen(["curl", "-s", "-o", "/dev/null", URL + LARGE],
                             stdout=subprocess.DEVNULL,
                             stderr=subprocess.DEVNULL)
        time.sleep(0.6 + 0.3 * i)
        p.kill()
        p.wait()
        print("  lan %d: da kill curl sau %.1fs" % (i + 1, 0.6 + 0.3 * i),
              flush=True)
    xv.run("setloss 0", r"set loss rate", 5)
    results.append(("client abort loss %d%%" % loss, rounds, rounds, []))
    return True


def phase_concurrent(xv, loss, conns, timeout, results):
    xv.run("setloss %d" % loss, r"set loss rate", 5)
    readme = os.path.getsize("README")
    print("Pha tai song song (loss %d%%): %d curl dong thoi" % (loss, conns),
          flush=True)
    procs = []
    t0 = time.time()
    for i in range(conns):
        path, expect = (LARGE, 1048576) if i % 2 == 0 else ("README", readme)
        p = subprocess.Popen(
            ["curl", "-s", "-o", "/dev/null", "-m", str(timeout), "-w",
             "%{http_code} %{size_download} %{time_total}", URL + path],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        procs.append((p, path, expect))
    ok = 0
    for p, path, expect in procs:
        out, _ = p.communicate()
        f = out.strip().split()
        good = len(f) >= 2 and f[0] == "200" and f[1] == str(expect)
        ok += good
    elapsed = time.time() - t0
    print("  thanh cong %d/%d trong %.1fs" % (ok, conns, elapsed), flush=True)
    results.append(("song song %d conn loss %d%%" % (conns, loss), ok, conns,
                    [elapsed]))
    xv.run("setloss 0", r"set loss rate", 5)
    return ok == conns


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--conns", type=int, default=8)
    ap.add_argument("--quick", action="store_true",
                    help="chi chay 1 lan moi pha")
    args = ap.parse_args()
    if args.quick:
        args.rounds = 1

    if not os.path.exists("kernel/kernel") or not os.path.exists("fs.img"):
        print("Chua build. Chay: make kernel/kernel fs.img")
        sys.exit(2)
    ref_sha = sha256_file("user/largefile.bin")

    xv = Xv6()
    time.sleep(2.5)
    if not xv.alive():
        print("QEMU khong khoi dong duoc (cong 20080 bi chiem?)")
        print(xv.text())
        sys.exit(1)

    failures = []
    audits = []
    results = []
    try:
        ok, a = audit(xv, "nen luc boot", wait=1)
        audits.append(("boot", a))
        if not ok:
            failures.append("kiem toan luc boot that bai")

        xv.run("httpd &", r"httpd: port", 5)
        time.sleep(1.0)

        def step(name, fn, audit_label):
            if not fn():
                failures.append(name)
            if not xv.alive():
                failures.append("QEMU chet sau pha " + name)
                raise RuntimeError("qemu died")
            ok, a = audit(xv, audit_label)
            audits.append((audit_label, a))
            if not ok:
                failures.append("ro ri sau pha " + name)

        step("loss 20%", lambda: phase_large(xv, 20, args.rounds, 120,
                                             ref_sha, results),
             "sau loss 20% (1MB)")
        step("loss 30%", lambda: phase_large(xv, 30, args.rounds, 180,
                                             ref_sha, results),
             "sau loss 30% (1MB)")
        step("loss 50%", lambda: phase_small(xv, 50, args.rounds, 90,
                                             results),
             "sau loss 50% (README)")
        step("client abort", lambda: phase_abort(xv, 30, max(3, args.rounds),
                                                 results),
             "sau client abort")
        step("song song", lambda: phase_concurrent(xv, 20, args.conns, 240,
                                                   results),
             "sau tai song song 20%")

        # server van song va phuc vu dung sau tat ca cac pha
        dl = "/tmp/w6_dl_final.bin"
        code, size, t = curl(LARGE, 30, dl)
        good = code == "200" and size == 1048576 and sha256_file(
            dl) == ref_sha
        print("Kiem tra hau stress (loss 0%%): http=%s size=%d time=%.2fs %s"
              % (code, size, t, "KHOP SHA-256" if good else "LOI"),
              flush=True)
        results.append(("hau stress loss 0% 1MB", int(good), 1, [t]))
        if not good:
            failures.append("server khong con phuc vu dung sau stress")

        ok, a = audit(xv, "bien ban cuoi (-f)", wait=60, flush=True)
        audits.append(("final", a))
        if not ok:
            failures.append("bien ban cuoi that bai")
        xv.run("memaudit -k", r"\[memaudit\] accounted", 5)
    except RuntimeError:
        pass
    finally:
        text = xv.text()
        xv.close()

    if re.search(r"panic", text):
        failures.append("kernel panic")
    if "[mbuf] BUG double free" in text:
        failures.append("double free mbuf")

    min_frees = [int(x) for x in re.findall(r"min_free=(\d+)", text)]
    print()
    print("=== TONG KET TUAN 6 (TV1) ===")
    for name, ok, total, times in results:
        extra = ""
        if times:
            extra = " (t: %s)" % ", ".join("%.2fs" % t for t in times)
        print("  %-28s %d/%d%s" % (name, ok, total, extra))
    if min_frees:
        print("  min_free thap nhat toan bai: %d/512" % min(min_frees))
    print("  so lan kiem toan PASS: %d/%d" %
          (sum(1 for _, a in audits if a.get("verdict") == "PASS"),
           len(audits)))
    print("  log console day du: %s" % LOG_PATH)
    if failures:
        print("FAIL: " + "; ".join(failures))
        sys.exit(1)
    print("PASS: stress test mat goi 20/30/50%, client abort, tai song song "
          "- zero memory leak (leaked=0, zombie=0)")


if __name__ == "__main__":
    main()
