import argparse
import inspect
import os
import re
import sys
import time
from scripts.simulator import QEMU

sys.stdout.reconfigure(line_buffering=True)

def crash_log():
    q = QEMU(reset=True)
    q.cmd("logstress f0 f1 f2 f3 f4 f5\n")
    time.sleep(2)
    q.crash()
    q.stop()


def recover_log():
    # Khoi dong lai va kiem tra tien trinh phuc hoi log
    q = QEMU()
    time.sleep(2)
    q.read()
    ok = q.match(r"^recovering", exit=False)
    if ok:
        q.cmd("ls\n")
        q.monitor(r"f5", timeout=30)
    q.stop()
    return ok


def forphan():
    # Tao file orphan va danh sap he thong
    q = QEMU(reset=True)
    q.cmd("forphan\n")
    q.monitor(r"wait", timeout=30)
    q.crash()
    q.stop()


def dorphan():
    # Tao thu muc orphan va danh sap he thong
    q = QEMU(reset=True)
    q.cmd("dorphan\n")
    q.monitor(r"wait", timeout=30)
    q.crash()
    q.stop()


def recover_orphan():
    # Kiem tra thu hoi orphan inode sau crash
    q = QEMU()
    q.monitor(r"^ireclaim", timeout=30)
    q.stop()


def test_log():
    print("Test recovery of log")
    for i in range(20):
        crash_log()
        ok = recover_log()
        if ok:
            print("OK")
            return
        print("log attempt", i + 1)
    print("FAIL")
    sys.exit(1)


def test_forphan():
    print("Test recovery of an orphaned file")
    forphan()
    recover_orphan()
    print("OK")


def test_dorphan():
    print("Test recovery of an orphaned file")
    dorphan()
    recover_orphan()
    print("OK")


def test_crash():
    test_log()
    test_forphan()
    test_dorphan()


def test_usertests(test="", quick=False):
    timeout = 300 if quick else 600
    opt = ""
    if quick:
        opt = " -q"
    elif test:
        opt = " " + test

    q = QEMU(reset=True)
    q.cmd("usertests" + opt + "\n")
    q.monitor(r"^ALL TESTS PASSED", progress=r"test", timeout=timeout)
    q.stop()


def main():
    parser = argparse.ArgumentParser(description="Script kiem thu he dieu hanh xv6")
    parser.add_argument("testrex", help="Ten test hoac bieu thuc regex")
    parser.add_argument("-q", action="store_true", help="Chay usertests che do nhanh")
    args = parser.parse_args()

    print(args)
    rex = r"%s" % args.testrex
    funcs = [
        (obj, name)
        for name, obj in inspect.getmembers(sys.modules[__name__])
        if (inspect.isfunction(obj) and name.startswith("test"))
    ]

    none = True
    for f, n in funcs:
        if re.search(rex, n):
            none = False
            if n == "test_usertests":
                f(quick=args.q)
            else:
                f()

    if none:
        test_usertests(test=args.testrex, quick=args.q)


if __name__ == "__main__":
    main()
