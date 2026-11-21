import os
import socket
import subprocess
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

def is_port_open(port=20080):
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=0.3)
        s.close()
        return True
    except Exception:
        return False

def start_qemu(boot_cmd="httpd\n", log_file=None):
    if is_port_open(20080):
        return None

    out = open(log_file, "w") if log_file else subprocess.DEVNULL
    proc = subprocess.Popen(
        QEMU_CMD,
        stdin=subprocess.PIPE,
        stdout=out,
        stderr=out
    )
    time.sleep(2.5)

    if boot_cmd and proc.stdin:
        proc.stdin.write(boot_cmd.encode() if isinstance(boot_cmd, str) else boot_cmd)
        proc.stdin.flush()
        time.sleep(1.0)
    return proc

def stop_qemu(proc):
    if not proc:
        return
    proc.terminate()
    try:
        proc.wait(timeout=2)
    except subprocess.TimeoutExpired:
        proc.kill()
