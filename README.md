# Minimal TCP/IP Stack & HTTP Server in xv6-riscv

A minimal in-kernel TCP/IP network stack, BSD socket layer, and HTTP/1.0 server implemented on xv6 (64-bit RISC-V).

## Features

- **Driver (E1000)**: Intel 82540EM DMA ring buffer (16 TX/RX descriptors) with hardware interrupt handling (`RXT0`). Includes an in-driver configurable packet loss simulator (0%–100%) controllable via system calls (`sys_set_loss_rate`, `sys_get_loss_rate`) and user utility (`setloss`).
- **Ethernet & ARP**: Ethernet II frame encapsulation, dynamic ARP cache with expiration, broadcast request, reply handling, and leak-free pending queue.
- **IPv4**: IPv4 header parsing/generation, RFC 1071 ones-complement checksum.
- **TCP (RFC 793)**: Full 11-state FSM, 4-tuple PCB lookup, sliding window flow control, half-close (`TCP_CLOSE_WAIT`), RFC 793 `RST` handling for client timeout/abort, dynamic RTO calculation (Jacobson/Karels, RFC 6298) calibrated to 10 Hz hardware timer, and Fast Retransmit on 3 duplicate ACKs (RFC 5681).
- **Socket API**: BSD-style syscalls (`socket`, `bind`, `listen`, `accept`, `connect`, `set_loss_rate`, `get_loss_rate`) integrated into xv6 VFS as `FD_SOCK` files with deadlock-free buffer allocation (`mbufalloc_wait`).
- **HTTP Server**: User-space HTTP/1.0 daemon (`httpd`) serving static files and streaming 1 MB test payloads with reliable delivery under 10% packet loss (completes in 4 seconds at ~252.5 KB/s with matching SHA-256 hash).

## Project Structure

```
kernel/
├── e1000.c, e1000_dev.h   # E1000 PCI NIC driver & configurable packet loss filter
├── eth.c                  # Ethernet framing (L2)
├── arp.c                  # ARP resolution and cache
├── ip.c                   # IPv4 input/output (L3)
├── checksum.c             # RFC 1071 Internet checksum
├── tcp.c                  # TCP PCB management, connection hash table, timer process
├── tcp_input.c            # TCP state transitions, ACK processing, RST recovery, fast retransmit
├── tcp_output.c           # TCP segment formatting, metadata tracking, and retransmission queue
├── tcp_timer.c            # Jacobson/Karels RTO update & periodic tick handler (10 Hz)
├── ringbuf.c              # Lock-protected FIFO ring buffer for sockets
├── sock.c, sysnet.c       # Socket abstraction, VFS integration, network syscalls
└── net.h                  # Network headers, structs, and protocol constants
user/
├── httpd.c                # HTTP/1.0 web server implementation
├── setloss.c              # Command-line utility to configure packet loss rate (0% - 100%)
└── user.h                 # User-space syscall prototypes
doc/
├── README.md              # Mục lục tiến trình thực hiện của 3 thành viên theo tuần
└── week_1/ .. week_6/     # Báo cáo tiến trình chi tiết của 3 thành viên theo từng tuần
plans/
├── README.md              # Kế hoạch tổng thể 6 tuần & Ma trận phân công (33%-33%-33%)
├── members/               # Kế hoạch chi tiết 6 tuần từng thành viên (TV1, TV2, TV3)
└── weeks/                 # Kế hoạch phối hợp từng tuần (Tuần 1 đến Tuần 6)
```

- **Báo cáo tiến trình từng thành viên theo tuần:** [doc/README.md](doc/README.md)
  - [Tuần 1](doc/week_1): [Phùng Huy Hoàng](doc/week_1/Phùng%20Huy%20Hoàng%20-%2020233414.md) (Giai đoạn 2) | [Nguyễn Vũ Hiệp](doc/week_1/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md) | [Lê Anh Đức](doc/week_1/Lê%20Anh%20Đức%20-%2020233324.md) (Giai đoạn 1)
  - [Tuần 2](doc/week_2) | [Tuần 3](doc/week_3) | [Tuần 4](doc/week_4) | [Tuần 5](doc/week_5) | [Tuần 6](doc/week_6)
- **Kế hoạch 6 tuần & Phân công nhiệm vụ chi tiết:** [plans/README.md](plans/README.md)
  - [Thành viên 1 (Hạ tầng mạng, mbuf, loss simulator & RTO)](plans/members/member_1_tv1.md)
  - [Thành viên 2 (Tầng giao vận TCP, Out-of-Order & Tắc nghẽn Reno/BBR)](plans/members/member_2_tv2.md)
  - [Thành viên 3 (Hệ thống tệp 1MB, Socket VFS, Multi-worker HTTP & Trực quan hóa)](plans/members/member_3_tv3.md)
  - Lộ trình từng tuần: [Tuần 1](plans/weeks/week_1.md) | [Tuần 2](plans/weeks/week_2.md) | [Tuần 3](plans/weeks/week_3.md) | [Tuần 4](plans/weeks/week_4.md) | [Tuần 5](plans/weeks/week_5.md) | [Tuần 6](plans/weeks/week_6.md)

## Requirements

Install the RISC-V cross-compiler and QEMU:

```bash
sudo apt update
sudo apt install -y gcc-riscv64-linux-gnu qemu-system-misc python3
```

## Build and Run

1. Compile kernel and user programs:
   ```bash
   make clean
   make -j$(nproc)
   ```

2. Start xv6 in QEMU:
   ```bash
   make qemu
   ```
   *(Exit QEMU: press `Ctrl-A` then `x`)*

## Testing

QEMU forwards host port `20080` to guest port `80` (`hostfwd=tcp::20080-:80`).

### 1. Configure Packet Loss Rate (xv6 shell)

The in-driver loss rate can be inspected and updated at runtime:

```sh
$ setloss
Current E1000 packet loss rate: 10%
Usage: setloss <rate_percent (0-100)>

$ setloss 0
E1000 packet loss rate set to 0%

$ setloss 10
E1000 packet loss rate set to 10%
```

### 2. Start the HTTP server

Inside the xv6 shell:
```sh
$ httpd
Starting xv6 httpd server (HTTP/1.0) on port 80...
```

### 3. Basic HTTP GET (Host)

From the Linux host terminal:
```bash
curl -v http://127.0.0.1:20080/README
```

### 4. 1 MB Transfer Test with 10% Packet Loss

The server exposes `/file1mb` which serves a deterministic 1,048,576-byte stream generated by `(i * 37 + 13) & 0xff`. Even with 10% packet drops enabled in the E1000 driver, TCP retransmissions ensure reliable delivery.

Download the file:
```bash
curl --max-time 60 http://127.0.0.1:20080/file1mb -o downloaded_1mb.bin
```

Generate the reference file and verify the SHA-256 hash:
```bash
python3 -c 'open("expected_1mb.bin", "wb").write(bytes(((i * 37 + 13) & 0xff) for i in range(1048576)))'
sha256sum downloaded_1mb.bin expected_1mb.bin
```

Expected output:
```text
c4e1ab33a4d2f0d7be4e6be6c387096da51a28bad003f71309bb774f75139111  downloaded_1mb.bin
c4e1ab33a4d2f0d7be4e6be6c387096da51a28bad003f71309bb774f75139111  expected_1mb.bin
```

### 5. Regression Tests

Run original xv6 system call tests to verify kernel stability:
```bash
python3 ./test-xv6.py usertests
```

## References

- RFC 791: Internet Protocol (IPv4)
- RFC 793: Transmission Control Protocol (TCP)
- RFC 826: Ethernet Address Resolution Protocol (ARP)
- RFC 1071: Computing the Internet Checksum
- RFC 5681: TCP Congestion Control & Fast Retransmit
- RFC 6298: Computing TCP's Retransmission Timer
- Intel 8254x 1000BASE-T Software Developer's Manual (E1000)
- MIT 6.S081 / xv6-riscv (https://github.com/mit-pdos/xv6-riscv)
