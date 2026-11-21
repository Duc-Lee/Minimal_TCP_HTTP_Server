import subprocess
import sys

ALL_TESTS = [
    "tests/tcp/test_state.py",
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

    for t in tests:
        print(f"\n>>> {t}")
        ret = subprocess.run([sys.executable, t]).returncode
        if ret != 0:
            sys.exit(ret)

    print("\ntat ca test deu pass")

if __name__ == "__main__":
    main()
