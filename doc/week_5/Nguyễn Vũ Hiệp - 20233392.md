# TIẾN TRÌNH TUẦN 5 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai: Hạ Tầng Bộ Đếm Phần Cứng RISC-V (`rdcycle`, `r_time`), Đồng Hồ Độ Phân Giải Microsecond, Cửa Sổ Trượt 10s `min_rtt` & Ước Lượng Băng Thông Hỗ Trợ Google BBR

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 5 (Week 5)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục tiêu

1. Xây dựng hạ tầng đọc thanh ghi bộ đếm phần cứng RISC-V ở cả Supervisor-mode (kernel) và User-mode: thanh ghi thời gian thực `time` (`r_time()`, `r_time_us()`), thanh ghi chu kỳ lệnh CPU `cycle` (`rdcycle()`, `r_cycle()`), và thanh ghi số lệnh hoàn tất `instret` (`r_instret()`), kèm cấu hình quyền truy cập trên `mcounteren` và `scounteren`.
2. Xây dựng module đồng hồ phần cứng `kernel/hwclock.c` tự động hiệu chuẩn tỷ lệ chu kỳ CPU trên microsecond (`cycle/µs`) theo bộ đếm CLINT `mtime` 10 MHz ngay khi khởi động kernel.
3. Thay thế phép đo RTT theo `ticks` 10 Hz (độ phân giải thô `1 tick = 100 ms`) bằng phép đo thời gian thực độ phân giải microsecond (`0.1 µs` trên phần cứng 10 MHz), tuân thủ nghiêm ngặt Quy tắc Karn.
4. Cung cấp API tính toán **cửa sổ trượt 10 giây `min_rtt`** (`TCP_MIN_RTT_WIN_US = 10,000,000 µs`) và **bộ ước lượng tốc độ chuyển phát `delivery_rate`** (`bytes/s`) trong `kernel/tcp_timer.c` để phục vụ động cơ kiểm soát tắc nghẽn **Google BBR** của TV2 và phép đo chu kỳ CPU cho **Zero-Copy `sys_sendfile`** của TV3.

---

## 2. Hiện trạng đầu tuần

Kiểm tra mã nguồn đầu Tuần 5 (trên nền commit `dcef9f4` mới nhất của nhánh `origin/main`) cho thấy các hạn chế sau khi hướng tới thuật toán Google BBR và đo đạc hiệu năng Zero-Copy:

| Hạng mục | Hiện trạng đầu tuần | Hệ quả đối với BBR & Benchmark `sys_sendfile` | Vị trí |
| :--- | :--- | :--- | :--- |
| Quyền đọc CSR bộ đếm (`mcounteren` / `scounteren`) | `timerinit()` chỉ bật bit `TM` (`1 << 1 = 2`) trong `mcounteren`, chưa bật `CY`/`IR` và chưa cấu hình `scounteren` | Gọi `csrr cycle` (`rdcycle`) trong S-mode hoặc U-mode sẽ gây lỗi trap **Illegal Instruction** (`scause = 2`) | `kernel/start.c`, `kernel/trap.c` |
| Hàm đọc chu kỳ CPU `rdcycle()` | Chưa được định nghĩa trong `kernel/riscv.h` hay `user/user.h` | Không đo được số chu kỳ CPU tiêu tốn khi so sánh `sys_sendfile` với `read()` + `write()` | `kernel/riscv.h`, `user/user.h` |
| Độ phân giải đo RTT | Đo bằng `dt = ticks - done->send_tick`, với `1 tick = 100 ms` (`sample_ms = (dt ? dt : 1) * 100`) | Mọi mẫu RTT trên mạng ảo QEMU (thực tế chỉ `~45–150 µs`) đều bị làm tròn cứng lên tối thiểu **`100 ms`** (sai lệch gấp ~1,000 lần), làm BBR tính sai BDP (`BDP = max_bw × min_rtt`) | `kernel/tcp_input.c`, `kernel/tcp_output.c` |
| Bộ lọc cửa sổ trượt 10 giây `min_rtt` | Chưa có trường lưu `min_rtt_us`, `min_rtt_stamp_us`, `min_rtt_expired` trong `struct tcp_pcb` | BBR không thể theo dõi độ trễ lan truyền tối thiểu của đường truyền hay kích hoạt pha `BBR_PROBE_RTT` khi mẫu hết hạn 10 giây | `kernel/net.h`, `kernel/tcp_timer.c` |
| Ước lượng băng thông chuyển phát (`delivery_rate`) | Chưa theo dõi lượng byte tích lũy `delivered` và khoảng thời gian chuyển phát trên từng segment | BBR không có đầu vào `delivered_rate` (`bytes/s`) để cập nhật băng thông cực đại `max_bw` | `kernel/net.h`, `kernel/tcp_timer.c` |

---

## 3. Chi Tiết Triển Khai

### 3.1. Mở quyền truy cập Hardware Counters và cài đặt `rdcycle()`, `r_time_us()` (`kernel/riscv.h`, `kernel/start.c`, `kernel/trap.c`, `user/user.h`)

* **Cơ chế phân quyền thanh ghi bộ đếm của kiến trúc RISC-V:**
  - Theo đặc tả RISC-V Privileged Architecture, các thanh ghi CSR chỉ đọc ở chế độ không đặc quyền gồm `cycle` (`0xC00`), `time` (`0xC01`), và `instret` (`0xC02`) bị kiểm soát bởi hai thanh ghi mặt nạ:
    - `mcounteren` (Machine Counter-Enable): quyết định S-mode được phép đọc counter nào.
    - `scounteren` (Supervisor Counter-Enable): quyết định U-mode được phép đọc counter nào.
  - Định nghĩa các hằng số bit trong `kernel/riscv.h`:
    ```c
    #define COUNTEREN_CY (1L << 0) // cycle
    #define COUNTEREN_TM (1L << 1) // time
    #define COUNTEREN_IR (1L << 2) // instret
    ```
  - Trong `timerinit()` (`kernel/start.c`), bật đầy đủ cả 3 bit `COUNTEREN_CY | COUNTEREN_TM | COUNTEREN_IR` trên `mcounteren`.
  - Trong `trapinithart()` (`kernel/trap.c`), bật `COUNTEREN_CY | COUNTEREN_TM | COUNTEREN_IR` trên `scounteren` cho từng hart, cho phép cả kernel lẫn tiến trình user-space (như công cụ `hwclock` hoặc benchmark `sendfile`) gọi trực tiếp lệnh `rdcycle` và `rdtime` mà không tốn chi phí ngữ cảnh syscall.
* **Định nghĩa tần số chuẩn CLINT `mtime` và các hàm đọc thanh ghi (`kernel/riscv.h`, `user/user.h`):**
  - Trên máy ảo QEMU `virt`, bộ đếm thời gian thực CLINT `mtime` chạy ở tần số cố định **`TIMEBASE_HZ = 10,000,000 Hz` (10 MHz)** — đúng như hằng số `1,000,000` ticks cho mỗi `100 ms` trong `timerinit()`. Do đó:
    $$\text{TIMEBASE\_PER\_US} = \frac{10,000,000}{1,000,000} = 10\text{ ticks}/\mu\text{s} \quad (1\text{ tick} = 100\text{ ns})$$
  - Cài đặt các hàm inline:
    - `rdcycle()` / `r_cycle()`: đọc CSR `cycle` trả về số chu kỳ xung nhịp CPU 64-bit.
    - `r_time_us()` / `rdtime_us()`: trả về `r_time() / TIMEBASE_PER_US` (thời gian thực tính bằng microsecond, độ phân giải phần cứng `0.1 µs`).
    - `r_instret()` / `rdinstret()`: đọc CSR `instret` trả về số lệnh đã hoàn tất.

### 3.2. Module hiệu chuẩn đồng hồ phần cứng `kernel/hwclock.c`

* Trong khi CSR `time` luôn chạy ở tần số cố định 10 MHz, CSR `cycle` trên QEMU TCG phản ánh bộ đếm chu kỳ xung nhịp của CPU thực thi. Để quy đổi chính xác giữa số chu kỳ lệnh `rdcycle()` và thời gian thực `µs`, module `kernel/hwclock.c` thực hiện hiệu chuẩn tự động trong `hwclock_init()` ngay khi boot (trên hart 0, trước khi bật ngắt ngoài):
  1. Đồng bộ vào sườn lên của thanh ghi `r_time()` để triệt tiêu sai số biên `100 ns`.
  2. Đo số chu kỳ CPU $\Delta C = c_1 - c_0$ trong khoảng thời gian chuẩn $\text{HWCLOCK\_CAL\_US} = 20,000\ \mu\text{s}$ ($20\text{ ms}$) theo đồng hồ `r_time()`.
  3. Tính hệ số quy đổi fixed-point nhân $1000$ (tránh hoàn toàn số thực dấu phẩy động trong kernel):
     $$\text{cyc\_per\_us\_fp} = \frac{(c_1 - c_0) \times 1000}{\Delta t_{\mu\text{s}}}$$
* Cung cấp bộ API hạt nhân trong `kernel/defs.h`:
  - `uint64 hwclock_now_us(void)`: thời gian thực hiện tại tính bằng `µs`.
  - `uint64 hwclock_uptime_us(void)`: số `µs` đã trôi qua từ lúc khởi tạo `hwclock_init()`.
  - `uint64 hwclock_cycles(void)`: đọc nhanh `rdcycle()`.
  - `uint64 hwclock_cycles_to_us(uint64 cycles)` và `uint64 hwclock_us_to_cycles(uint64 us)`: quy đổi hai chiều giữa chu kỳ CPU và microsecond theo hệ số đã hiệu chuẩn.

### 3.3. Bộ lọc cửa sổ trượt 10 giây `min_rtt` và ước lượng băng thông `delivery_rate` hỗ trợ Google BBR (`kernel/net.h`, `kernel/tcp_timer.c`)

* **Mở rộng metadata trên `struct mbuf` và `struct tcp_pcb` (`kernel/net.h`):**
  - Trong `struct mbuf`: bổ sung `uint64 send_us`, `uint64 tx_delivered`, `uint64 tx_delivered_us`, `uint64 tx_first_sent_us` (được reset sạch sẽ trong `mbuf_take_locked()` tại `kernel/mbuf.c`).
  - Trong `struct tcp_pcb`: bổ sung nhóm trường đo lường độ phân giải cao cho BBR:
    - `uint32 latest_rtt_us`: mẫu RTT hợp lệ gần nhất (`µs`).
    - `uint32 min_rtt_us`: RTT nhỏ nhất ghi nhận trong cửa sổ trượt 10 giây (`µs`).
    - `uint64 min_rtt_stamp_us`: thời điểm (`µs`) cập nhật `min_rtt_us`.
    - `int min_rtt_expired`: cờ báo `1` khi mẫu mới làm cửa sổ 10 giây hết hạn (tín hiệu để BBR chuyển vào pha `BBR_PROBE_RTT`).
    - `uint32 rtt_samples`: tổng số mẫu RTT hợp lệ đã thu thập.
    - `uint64 delivered`, `uint64 delivered_us`, `uint64 first_sent_us`: bộ đếm byte tích lũy đã được ACK và mốc thời gian tương ứng.
    - `uint32 delivery_rate`, `uint32 rate_interval_us`, `uint32 rate_delivered`: mẫu băng thông chuyển phát gần nhất (`bytes/s`).
* **Thuật toán cửa sổ trượt 10 giây `min_rtt` (`tcp_update_min_rtt` trong `kernel/tcp_timer.c`):**
  - Với hằng số `#define TCP_MIN_RTT_WIN_US 10000000ULL` ($10\text{ s} = 10,000,000\ \mu\text{s}$):
    ```c
    int
    tcp_min_rtt_expired(struct tcp_pcb *pcb, uint64 now_us)
    {
      return pcb->min_rtt_us != 0 &&
             now_us - pcb->min_rtt_stamp_us > TCP_MIN_RTT_WIN_US;
    }

    int
    tcp_update_min_rtt(struct tcp_pcb *pcb, uint32 rtt_us, uint64 now_us)
    {
      int expired = tcp_min_rtt_expired(pcb, now_us);

      if (rtt_us == 0)
        rtt_us = 1;
      if (pcb->min_rtt_us == 0 || rtt_us <= pcb->min_rtt_us || expired) {
        pcb->min_rtt_us = rtt_us;
        pcb->min_rtt_stamp_us = now_us;
      }
      return expired;
    }
    ```
  - Khi có mẫu `rtt_us` mới nhỏ hơn hoặc bằng `min_rtt_us` hiện tại, hoặc khi `min_rtt_us` cũ đã quá 10 giây chưa được làm mới (`expired == 1`), bộ lọc lập tức cập nhật giá trị mới và làm mới mốc thời gian `min_rtt_stamp_us`.
* **Ước lượng băng thông chuyển phát `delivery_rate` chuẩn BBR (`tcp_rate_on_send`, `tcp_rate_on_ack`):**
  - Khi gửi segment lần đầu trong `tcp_output()`, hàm `tcp_rate_on_send(pcb, clone)` chụp lại trạng thái chuyển phát hiện tại của kết nối (`pcb->delivered`, `pcb->delivered_us`, `pcb->first_sent_us`) gắn vào `clone` trong `retransmit_q`. Nếu hàng đợi truyền lại trước đó đang rỗng (kết nối vừa thoát trạng thái nghỉ অ্যাপ-limited), mốc `first_sent_us` và `delivered_us` được đặt lại bằng `now_us` để không tính thời gian ứng dụng nghỉ vào mẫu băng thông.
  - Khi nhận ACK xác nhận dữ liệu mới trong `tcp_handle_ack()` (`kernel/tcp_input.c`), chỉ các segment chưa từng bị truyền lại (`!done->retransmitted` — **Quy tắc Karn**) mới được chụp mẫu qua `tcp_rate_snapshot(done, &rs)`.
  - Hàm `tcp_rate_on_ack(pcb, acked, now_us, have_rs ? &rs : 0)` tính toán:
    1. Mẫu RTT tức thời: $\text{rtt\_us} = \max(1,\ \text{now\_us} - \text{rs.send\_us})$.
    2. Cập nhật `min_rtt_us` qua `tcp_update_min_rtt()`.
    3. Tính khoảng thời gian chuyển phát theo đặc tả IETF `draft-cheng-iccrg-delivery-rate-estimation`:
       $$\text{send\_elapsed} = \text{rs.send\_us} - \text{rs.first\_sent\_us}, \quad \text{ack\_elapsed} = \text{now\_us} - \text{rs.prior\_us}$$
       $$\text{interval} = \max(\text{send\_elapsed},\ \text{ack\_elapsed}), \quad \Delta\text{delivered} = \text{pcb}\to\text{delivered} - \text{rs.prior\_delivered}$$
       $$\text{delivery\_rate} = \frac{\Delta\text{delivered} \times 1,000,000}{\text{interval}}\quad (\text{bytes/s})$$
       Việc lấy $\max(\text{send\_elapsed}, \text{ack\_elapsed})$ giúp chống hiện tượng nén ACK (ACK compression) làm ước lượng vọt lố băng thông thực.
  - Đồng thời, mẫu `sample_ms` truyền vào `tcp_update_rto()` (RFC 6298) được tính trực tiếp từ `rtt_us` làm tròn lên millisecond: `sample_ms = (rtt_us + 999) / 1000` (tối thiểu `1 ms`), thay vì bị lượng tử hóa thô ở mức `100 ms` như các tuần trước.

### 3.4. Mở rộng `struct tcp_stat`, an toàn ngăn xếp `sys_netstat` & Công cụ kiểm chứng `user/hwclock.c`

* Bổ sung 3 trường `uint32 rtt_us`, `uint32 min_rtt_us`, `uint32 delivery_rate` vào `struct tcp_stat` (`kernel/net.h` và `user/user.h`), được sao chép trong `tcp_get_stats()` và in chi tiết trong `tcp_dump_stats()` (`kernel/tcp.c`) để phục vụ cả BBR (TV2) lẫn Dashboard `tcptrace` / `netstat` (TV3).
* **Bảo vệ chống tràn stack kernel trong `sys_netstat()` (`kernel/sysnet.c`):** Khi `struct tcp_stat` mở rộng thêm 12 bytes/entry, mảng 32 phần tử chiếm hơn 2 KB; để tránh nguy cơ tràn trang stack kernel 4 KB khi có ngắt mạng E1000 lồng vào, `sys_netstat()` đã được nâng cấp sang cấp phát động 1 trang qua `kalloc()` và `kfree()` ngay sau khi `either_copyout()` hoàn tất.
* Xây dựng tiện ích user-space `user/hwclock.c` và kịch bản kiểm thử tự động `tests/test_hwclock_bbr.py` để kiểm chứng toàn diện các bộ đếm phần cứng và thông số thời gian thực của kết nối TCP.

---

## 4. Kết Quả Kiểm Thử

Môi trường: QEMU `virt` (3 harts, 128 MB RAM), card mạng Intel E1000 (`hostfwd tcp::20080-:80`), biên dịch với `-Wall -Werror` sạch 100% không có bất kỳ cảnh báo nào.

### 4.1. Khởi động Kernel, Hiệu chuẩn `hwclock_init()` và Self-test Bộ lọc 10s `min_rtt`

Ngay khi kernel boot, `hwclock_init()` và `tcp_timer_selftest()` tự động chạy và in kết quả xác nhận trên console:
```text
xv6 kernel is booting

hwclock: timebase=10000000 Hz (10 tick/us), cpu=3109.653 cycle/us (calib 20007 us, 62214833 cycles)
PCI found dev 0: vendor/dev=0x81b36
PCI found dev 1: vendor/dev=0x100e8086
Configuring E1000 on dev 1...
e1000: rx_ring=32 tx_ring=32
tcp_timer: selftest 10s min_rtt window & delivery_rate OK
```
* `tcp_timer_selftest()` kiểm chứng tất cả các trường hợp biên của bộ lọc cửa sổ trượt 10 giây:
  1. Cập nhật giảm dần `1200 µs -> 800 µs` và giữ nguyên `800 µs` trước mẫu `1500 µs` khi còn nằm trong cửa sổ `10,000,000 µs`.
  2. Tự động hết hạn cửa sổ (`expired = 1`) khi mẫu mới đến sau `10.5 giây`, nhận mẫu `1400 µs` làm `min_rtt_us` mới.
  3. Kiểm chứng phép tính `delivery_rate = 14,000,000 B/s` khi chuyển phát `14,000 bytes` trong `1,000 µs`, và đảm bảo Quy tắc Karn bỏ qua mẫu thời gian khi segment bị đánh dấu truyền lại.

### 4.2. Kiểm chứng `rdcycle()`, `r_time()`, Độ phân giải & Tính đơn điệu (`user/hwclock.c`)

Chạy lệnh `hwclock` trực tiếp trong shell xv6:
```text
$ hwclock
== hwclock: bo dem phan cung RISC-V ==
timebase      : 10000000 Hz (10 tick = 1 us, 1 tick = 100 ns)
time hien tai : 24851974 tick = 2485201 us
cycle hien tai: 3214148551206
instret       : 3214159278461

== hieu chuan (pause 10 tick) ==
xv6 ticks     : 10 tick (~1000 ms theo 100 ms/tick)
dong ho time  : 954101 us
cycle         : 2967625352 chu ky
ty le         : 3110.387 cycle/us

== do phan giai ==
time  : buoc nho nhat 1 tick = 100 ns
cycle : buoc nho nhat 155 chu ky

== kiem tra don dieu (100000 mau, 20 lan doi lich) ==
time  lui: 0 lan -> OK
cycle lui: 0 lan -> OK

== benchmark memmove 1 MB (khoi 4 KB) ==
cycle : 4966408 chu ky (4850 chu ky/KB)
time  : 1602 us (quy doi tu cycle: 1596 us)
bang thong copy: 654 MB/s
```
* **Độ chính xác quy đổi `cycle -> µs`:** Khi đo `memmove` 1 MB (`4,966,408` chu kỳ CPU), thời gian thực đo bằng `r_time()` là **`1,602 µs`**, trong khi thời gian quy đổi từ số chu kỳ `rdcycle()` qua hệ số hiệu chuẩn là **`1,596 µs`** (sai số chỉ `6 µs`, tức **< 0.4%**).
* **Tính đơn điệu đa nhân:** Lấy `100,000` mẫu liên tiếp xen kẽ `20` lần nhường CPU (`pause(1)`) trên hệ thống SMP 3 harts: `0` lần lùi giá trị ở cả `time` lẫn `cycle`.

### 4.3. Đo đạc `rtt_us`, `min_rtt_us` và `delivery_rate` thực tế khi tải `largefile.bin` (1 MB)

Chạy `python3 tests/test_hwclock_bbr.py` khởi động `httpd &` và tải `largefile.bin` (1 MB):
```text
    :80 -> :44490 ESTAB rtt_us=139 min_rtt_us=46 rate=9835 KB/s cwnd=44800 srtt=1 ms rto=200 ms
    :80 -> :44490 ESTAB rtt_us=89 min_rtt_us=46 rate=14220 KB/s cwnd=44800 srtt=1 ms rto=200 ms
=== KET QUA KIEM THU TUAN 5 (TV1) ===
PASS: Tat ca kiem thu rdcycle, r_time, 10s min_rtt window va delivery_rate thanh cong!
```

Bảng so sánh độ chính xác đo lường trước và sau khi nâng cấp Tuần 5:

| Thông số đo | Trước Tuần 5 (Tick 10 Hz) | Sau Tuần 5 (Hardware Counter 10 MHz) | Ý nghĩa đối với hệ thống |
| :--- | :--- | :--- | :--- |
| **Độ phân giải thời gian** | `100,000 µs` (`100 ms/tick`) | **`0.1 µs`** (`100 ns/tick`, hiển thị `1 µs`) | Chính xác gấp **$10^6$ lần** về độ phân giải phần cứng |
| **Mẫu RTT ghi nhận trên QEMU LAN** | Luôn bị làm tròn lên `100 ms` | **`46 – 139 µs`** (`srtt = 1 ms`) | Phản ánh đúng độ trễ thực của đường truyền ảo hóa |
| **Cửa sổ trượt 10s `min_rtt_us`** | Không có | **`46 µs`** (tự động hết hạn sau $10^7\ \mu\text{s}$) | Sẵn sàng cho BBR tính BDP và chuyển pha `PROBE_RTT` |
| **Tốc độ chuyển phát `delivery_rate`** | Không có | **`9,835 – 14,220 KB/s`** (~9.8 – 14.2 MB/s) | Sẵn sàng cho BBR ước lượng `max_bw` |

### 4.4. Kiểm thử hồi quy toàn hệ thống

1. **Bộ test máy trạng thái FSM (`python3 tests/run_tests.py`):** **6/6 PASS** (`3way_handshake`, `closed_port_rst`, `half_close`, `fragmented_stream`, `largefile_1mb`, `client_abort`).
2. **Bộ test HTTP/1.1 & Worker Pool (`python3 tests/test_http11.py`):** **5/5 PASS** (`test_index_and_mime`, `test_favicon_204`, `test_keep_alive`, `test_long_headers`, `test_worker_pool`).
3. **Bộ test tải nặng đồng thời (`python3 tests/concurrent_stress.py --conns 8 --rounds 2`):** **16/16 request thành công (100%)**, pool `mbuf` thu hồi tuyệt đối về mức nền `free = 480/512` (0 rò rỉ bộ nhớ).

---

## 5. Phối Hợp Nhóm & Việc Tiếp Theo

* **Bàn giao cho TV2 (Phùng Huy Hoàng – Google BBR trong `kernel/tcp_cc.c`):**
  - TV2 có thể truy cập trực tiếp các trường đã được cập nhật tự động trên mỗi ACK hợp lệ trong `struct tcp_pcb`:
    - `pcb->latest_rtt_us`: mẫu RTT vừa đo (`µs`).
    - `pcb->min_rtt_us` (hoặc gọi `tcp_get_min_rtt_us(pcb)`): RTT nhỏ nhất trong cửa sổ 10 giây (`µs`).
    - `pcb->min_rtt_expired`: bằng `1` khi cửa sổ 10 giây vừa hết hạn để kích hoạt pha `BBR_PROBE_RTT`.
    - `pcb->delivery_rate`: băng thông chuyển phát tức thời (`bytes/s`) để cập nhật `max_bw`.
    - `tcp_now_us()`: lấy mốc thời gian hiện tại theo `µs`.
* **Bàn giao cho TV3 (Lê Anh Đức – Zero-Copy `sys_sendfile` & `tcptrace`):**
  - TV3 có thể gọi trực tiếp `rdcycle()` (hoặc `hwclock_cycles()` và `hwclock_cycles_to_us()`) trong `kernel/sendfile.c` cũng như từ user-space để đo số chu kỳ CPU tiết kiệm được của `sys_sendfile` so với `read()/write()`.
  - `struct tcp_stat` trả về từ syscall `netstat()` đã có sẵn `rtt_us`, `min_rtt_us` và `delivery_rate` để hiển thị trên dashboard `user/tcptrace.c`.

---

## 6. Danh Sách Tệp Thay Đổi

| Tệp | Thay đổi |
| :--- | :--- |
| `kernel/riscv.h` | Định nghĩa `COUNTEREN_CY`, `COUNTEREN_TM`, `COUNTEREN_IR`, `TIMEBASE_HZ` (10 MHz), `TIMEBASE_PER_US` (10); cài đặt `rdcycle()`, `r_cycle()`, `r_instret()`, `r_time_us()`, `w_scounteren()`, `r_scounteren()` |
| `kernel/start.c` | Bật `COUNTEREN_CY \| COUNTEREN_TM \| COUNTEREN_IR` trong `mcounteren` tại `timerinit()` |
| `kernel/trap.c` | Bật `COUNTEREN_CY \| COUNTEREN_TM \| COUNTEREN_IR` trong `scounteren` tại `trapinithart()` cho phép user-space đọc bộ đếm phần cứng |
| `kernel/hwclock.c` | Module mới: hiệu chuẩn tỷ lệ `cycle/µs` theo đồng hồ `r_time()` lúc boot (`hwclock_init`), cung cấp API `hwclock_now_us`, `hwclock_uptime_us`, `hwclock_cycles`, `hwclock_cycles_to_us`, `hwclock_us_to_cycles` |
| `kernel/defs.h`, `kernel/main.c` | Khai báo các hàm `hwclock_*` và gọi `hwclock_init()` khi khởi động kernel |
| `kernel/net.h` | Mở rộng `struct mbuf`, `struct tcp_pcb`, `struct tcp_stat`; định nghĩa `TCP_MIN_RTT_WIN_US` (10s), `struct tcp_rate_sample` và khai báo các hàm đo RTT/băng thông độ phân giải `µs` |
| `kernel/tcp_timer.c` | Hiện thực `tcp_now_us()`, `tcp_rate_init()`, `tcp_rate_on_send()`, `tcp_rate_snapshot()`, `tcp_rate_on_ack()`, `tcp_update_min_rtt()`, `tcp_get_min_rtt_us()`, `tcp_min_rtt_expired()`, và `tcp_timer_selftest()` |
| `kernel/mbuf.c` | Khởi tạo các trường timestamp `send_us`, `tx_delivered`, `tx_delivered_us`, `tx_first_sent_us` trong `mbuf_take_locked()` |
| `kernel/tcp_output.c` | Gọi `tcp_rate_on_send(pcb, clone)` khi đưa segment vào `retransmit_q` |
| `kernel/tcp_input.c` | Đo RTT độ phân giải `µs` theo Quy tắc Karn trong `tcp_handle_ack()`, gọi `tcp_rate_on_ack()` cập nhật `min_rtt_us` 10s và `delivery_rate` |
| `kernel/tcp.c`, `kernel/sysnet.c` | Khởi tạo `tcp_rate_init(pcb)`, gọi `tcp_timer_selftest()`, xuất `rtt_us`/`min_rtt_us`/`delivery_rate` qua `tcp_get_stats()` & `tcp_dump_stats()`, dùng `kalloc()` cho buffer `sys_netstat` |
| `user/user.h`, `user/hwclock.c`, `Makefile` | Đồng bộ `struct tcp_stat`, thêm `rdcycle()`/`rdtime()`/`rdinstret()`/`rdtime_us()` cho user-space, xây dựng tiện ích `hwclock` và đăng ký vào `Makefile` |
| `tests/test_hwclock_bbr.py` | Kịch bản kiểm thử tự động bộ đếm phần cứng RISC-V, cửa sổ trượt 10s `min_rtt` và `delivery_rate` khi truyền tải file 1 MB |
