import argparse
import os
import re
import signal
import subprocess
import sys
import time


class QEMUSimulator:
    def __init__(self, reset=False, cwd=None, output_file="test-xv6.out"):

        if cwd:
            self.cwd = os.path.abspath(cwd)
        else:
            self.cwd = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.output_file = output_file
        self.output = ""
        self.outbytes = bytearray()
        self.reported = 0

        if reset:
            self.build_xv6()
            self.reset_fs()

        self.start()

    def reset_fs(self):
        # Tao lai file he thong fs.img sach
        try:
            subprocess.run(["rm", "-f", "fs.img"], cwd=self.cwd, check=True)
            subprocess.run(["make", "fs.img"], cwd=self.cwd, check=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")

    def build_xv6(self):
        # Bien dich kernel
        try:
            subprocess.run(["make", "kernel/kernel"], cwd=self.cwd, check=True)
        except subprocess.CalledProcessError as e:
            print(f"Command failed with exit code {e.returncode}")

    def start(self):
        # Khoi dong tien trinh qemu qua make
        cmd = ["make", "qemu"]
        self.proc = subprocess.Popen(
            cmd,
            cwd=self.cwd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        )
        os.set_blocking(self.proc.stdout.fileno(), False)
        time.sleep(1)

    def save_output(self, path=None):
        # Ghi toan bo log console ra file
        filepath = path or self.output_file or "test-xv6.out"
        try:
            with open(filepath, "w", encoding="utf-8") as f:
                f.write(self.output)
        except OSError as e:
            print(f"Provided a bad results path. Error: {e}")

    def cmd(self, c):
        # Gui lenh vao shell xv6
        if isinstance(c, str):
            c = c.encode("utf-8")
        self.proc.stdin.write(c)
        self.proc.stdin.flush()

    def crash(self):
        # Mo phong su co tat nguon dot ngot bang cach kill truc tiep tien trinh qemu con
        ps = subprocess.run(
            ["ps", "-opid", "--no-headers", "--ppid", str(self.proc.pid)],
            stdout=subprocess.PIPE,
            encoding="utf-8"
        )
        kids = [int(line.strip()) for line in ps.stdout.splitlines() if line.strip()]
        if not kids:
            print("no qemu")
            sys.exit(1)
        print("kill", kids[0])
        try:
            os.kill(kids[0], signal.SIGKILL)
        except ProcessLookupError:
            pass

    def stop(self):
        # Dung sach tien trinh qemu va don cac process con tranh ro ri port
        if self.proc and self.proc.poll() is None:
            try:
                ps = subprocess.run(
                    ["ps", "-opid", "--no-headers", "--ppid", str(self.proc.pid)],
                    stdout=subprocess.PIPE,
                    encoding="utf-8"
                )
                for line in ps.stdout.splitlines():
                    val = line.strip()
                    if val.isdigit():
                        try:
                            os.kill(int(val), signal.SIGKILL)
                        except (ProcessLookupError, ValueError):
                            pass
            except Exception:
                pass

            try:
                self.proc.terminate()
                self.proc.wait(timeout=2)
            except (subprocess.TimeoutExpired, Exception):
                try:
                    self.proc.kill()
                except Exception:
                    pass

    def read(self):
        # Doc du lieu console non-blocking
        while True:
            try:
                buf = os.read(self.proc.stdout.fileno(), 4096)
            except BlockingIOError:
                break
            if len(buf) == 0:
                break
            self.outbytes.extend(buf)
        self.output = self.outbytes.decode("utf-8", "replace")

    def lines(self):
        return self.output.splitlines()

    def error(self, *regexps):
        print("FAIL: match failed", regexps)
        self.save_output()
        self.stop()
        sys.exit(1)

    def match(self, *regexps, exit=True):
        found = False
        for line in self.lines():
            if any(re.match(r, line) for r in regexps):
                print(line)
                found = True
        if not found and exit:
            self.error(*regexps)
        return found

    def progress(self, regexp):
        end = self.output.rfind("\n") + 1
        if end <= self.reported:
            return
        for line in self.output[self.reported:end].splitlines():
            if re.match(regexp, line):
                print(line)
        self.reported = end

    def monitor(self, *regexps, progress="", timeout=30):
        deadline = time.time() + timeout
        while True:
            time.sleep(1)
            timeleft = deadline - time.time()
            if timeleft < 0:
                self.error(*regexps)
            self.read()
            if progress:
                self.progress(progress)
            if self.match(*regexps, exit=False):
                return

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()


# Alias tuong thich ten class cu
QEMU = QEMUSimulator


def main():
    parser = argparse.ArgumentParser(description="Bo gia lap QEMU xv6")
    parser.add_argument("--reset", action="store_true", help="Build lai kernel va fs.img")
    parser.add_argument("--cmd", type=str, help="Gui lenh vao xv6 shell")
    parser.add_argument("--monitor", type=str, help="Cho regex tren console")
    parser.add_argument("--timeout", type=int, default=30, help="Timeout (giay)")
    args = parser.parse_args()

    q = QEMUSimulator(reset=args.reset)
    try:
        if args.cmd:
            q.cmd(args.cmd + "\n")
            if args.monitor:
                q.monitor(args.monitor, timeout=args.timeout)
            else:
                time.sleep(2)
                q.read()
                print(q.output)
        else:
            time.sleep(2)
            q.read()
            print(q.output)
    finally:
        q.stop()


if __name__ == "__main__":
    main()

