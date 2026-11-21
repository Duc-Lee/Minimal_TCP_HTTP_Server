#!/usr/bin/env python3
# test runner trung tam cho toan bo he thong

import os
import subprocess
import sys

BASE_DIR = os.path.dirname(os.path.abspath(__file__))

TARGETS = {
    "state": ("tcp/test_state.py", "tcp state fsm"),
    "http11": ("http/test_http11.py", "http/1.1 keep-alive"),
    "sendfile": ("http/test_sendfile.py", "zero-copy sendfile"),
    "bbr": ("tcp/test_bbr.py", "bbr congestion control"),
    "hwclock": ("tcp/test_hwclock.py", "hardware timer va rtt"),
    "stress": ("tcp/test_stress.py", "tai song song"),
}

GROUPS = {
    "core": ["state", "http11", "sendfile"],
    "tcp": ["state", "bbr", "hwclock", "stress"],
    "http": ["http11", "sendfile"],
    "all": ["state", "http11", "sendfile", "bbr", "hwclock", "stress"],
}

def run_script(rel_path):
    full_path = os.path.join(BASE_DIR, rel_path)
    res = subprocess.run([sys.executable, full_path])
    return res.returncode == 0

def main():
    arg = sys.argv[1].lower() if len(sys.argv) > 1 else "all"

    if arg in GROUPS:
        selected = GROUPS[arg]
    elif arg in TARGETS:
        selected = [arg]
    else:
        print(f"khong tim thay target '{arg}'. danh sach: {list(GROUPS.keys()) + list(TARGETS.keys())}")
        return 1

    failed = 0
    for name in selected:
        rel_path, desc = TARGETS[name]
        print(f"--- {desc} ({rel_path}) ---")
        ok = run_script(rel_path)
        if not ok:
            failed += 1
        print()

    if failed == 0:
        print(f"tat ca {len(selected)} bai test deu pass")
        return 0
    else:
        print(f"{failed}/{len(selected)} bai test fail")
        return 1

if __name__ == "__main__":
    sys.exit(main())
