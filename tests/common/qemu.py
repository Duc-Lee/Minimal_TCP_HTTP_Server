import os
import socket
import subprocess
import sys
import time

QEMU_CMD = [
    "qemu-system-riscv64",
    "-machine", "virt",
    "-bios", "none",
    "-kernel", "kernel/kernel",
    "-m", "128M",
    "-smp", "3",
    "-nographic",
    "-global", "virtio-mmio.force-legacy=false",
    "-drive", "file=fs.img,if=none,format=raw,id=x0",
    "-device", "virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0",
    "-netdev", "user,id=net0,hostfwd=tcp::20080-:80,hostfwd=tcp::20081-:81",
    "-device", "e1000,netdev=net0,bus=pcie.0"
]

def check_build():
    if not os.path.exists("kernel/kernel") or not os.path.exists("fs.img"):
        print("chua build: chay make kernel/kernel fs.img")
        sys.exit(2)

def is_port_open(host="127.0.0.1", port=20080, timeout=0.5):
    try:
        s = socket.create_connection((host, port), timeout=timeout)
        s.close()
        return True
    except Exception:
        return False

class Qemu:
    def __init__(self, log_path=None):
        check_build()
        self.log_path = log_path
        self.proc = None
        self.log_file = None

    def start(self, boot_cmd="httpd\n", wait_boot=2.5):
        if self.log_path:
            self.log_file = open(self.log_path, "w")
            stdout_target = self.log_file
        else:
            stdout_target = subprocess.DEVNULL

        self.proc = subprocess.Popen(
            QEMU_CMD,
            stdin=subprocess.PIPE,
            stdout=stdout_target,
            stderr=subprocess.STDOUT if self.log_path else subprocess.DEVNULL
        )

        time.sleep(wait_boot)
        if self.proc.poll() is not None:
            if self.log_file:
                self.log_file.flush()
            print("qemu khoi dong loi (port 20080 dang bi chiem?)")
            sys.exit(1)

        if boot_cmd:
            self.send(boot_cmd)
            time.sleep(1.0)

    def send(self, cmd_str):
        if self.proc and self.proc.stdin:
            if isinstance(cmd_str, str):
                cmd_str = cmd_str.encode()
            self.proc.stdin.write(cmd_str)
            self.proc.stdin.flush()

    def read_log(self):
        if not self.log_path or not os.path.exists(self.log_path):
            return ""
        if self.log_file:
            self.log_file.flush()
        with open(self.log_path, "r", errors="replace") as f:
            return f.read()

    def stop(self):
        if self.proc:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()
            self.proc = None
        if self.log_file:
            self.log_file.close()
            self.log_file = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()

def ensure_qemu(boot_cmd="httpd\n", port=20080):
    # neu da co qemu dang chay tren port thi dung lai, khong khoi dong moi
    if is_port_open(port=port):
        return None
    q = Qemu()
    q.start(boot_cmd=boot_cmd)
    return q
