import os
import socket
import subprocess
import time

def build_qemu_cmd(port=20080, rst_port=20081):
    return [
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
        "-netdev", f"user,id=net0,hostfwd=tcp::{port}-:80,hostfwd=tcp::{rst_port}-:81",
        "-device", "e1000,netdev=net0,bus=pcie.0",
    ]

def is_port_open(port=20080):
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=0.3)
        s.close()
        return True
    except Exception:
        return False

def start_qemu(boot_cmd="httpd\n", log_file=None, port=None, rst_port=None):
    if port is None:
        port = int(os.environ.get("XV6_PORT", 20080))
    if rst_port is None:
        rst_port = int(os.environ.get("XV6_RST_PORT", port + 1))

    if is_port_open(port):
        return None

    out = open(log_file, "w") if log_file else subprocess.DEVNULL
    proc = subprocess.Popen(
        build_qemu_cmd(port, rst_port),
        stdin=subprocess.PIPE,
        stdout=out,
        stderr=out
    )
    if log_file and out is not subprocess.DEVNULL:
        proc._log_fh = out
    time.sleep(2.5)

    if proc.poll() is not None:
        stop_qemu(proc)
        raise RuntimeError(f"QEMU thoat som voi ma {proc.returncode} (port {port})")

    if boot_cmd and proc.stdin:
        proc.stdin.write(boot_cmd.encode() if isinstance(boot_cmd, str) else boot_cmd)
        proc.stdin.flush()
        time.sleep(1.0)
    return proc

def stop_qemu(proc):
    if not proc:
        return
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)
    fh = getattr(proc, "_log_fh", None)
    if fh:
        fh.close()
    time.sleep(0.3)
