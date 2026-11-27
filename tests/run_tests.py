import os
import subprocess
import sys

ALL_TESTS = [
    "tests/tcp/test_state.py",
    "tests/tcp/test_edge_cases.py",
    "tests/http/test_http11.py",
    "tests/http/test_sendfile.py",
    "tests/tcp/test_bbr.py",
    "tests/tcp/test_hwclock.py",
    "tests/tcp/test_stress.py",
]

def main():
    tests = ALL_TESTS
    if len(sys.argv) > 1:
        kw = sys.argv[1].lower()
        tests = [t for t in ALL_TESTS if kw in t]
        if not tests:
            print("khong tim thay test:", sys.argv[1])
            sys.exit(1)

    base_port = int(os.environ.get("XV6_BASE_PORT", 20080))
    for idx, t in enumerate(tests):
        port = base_port + idx * 2
        rst_port = port + 1
        cmd = [sys.executable, t, "--port", str(port)]
        if "test_state.py" in t or "test_edge_cases.py" in t:
            cmd += ["--rst-port", str(rst_port)]
        env = os.environ.copy()
        env["XV6_PORT"] = str(port)
        env["XV6_RST_PORT"] = str(rst_port)

        print(f"\n>>> {t} (port={port})")
        ret = subprocess.run(cmd, env=env).returncode
        if ret != 0:
            sys.exit(ret)

    print("\ntat ca test deu pass")

if __name__ == "__main__":
    main()
