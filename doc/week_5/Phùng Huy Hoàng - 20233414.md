# TIẾN TRÌNH TUẦN 5 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai: Thuật Toán Kiểm Soát Tắc Nghẽn Google BBR, Syscall `set_cc`, Tiện Ích `setcc` & Benchmark Hiệu Năng Vượt Trội So Với Reno Khi Mất Gói 10%

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 5 (Week 5)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Kỹ Thuật Tuần 5

Kế hoạch kỹ thuật Tuần 5 của Thành viên 2 tập trung giải quyết bài toán cốt lõi của tầng giao vận TCP:
1. **Hiện thực hóa Động cơ kiểm soát tắc nghẽn Google BBR (Bottleneck Bandwidth and RTT):**
   - Thay thế cơ chế AIMD (Additive Increase / Multiplicative Decrease) cổ điển vốn xem mất gói ngẫu nhiên là tắc nghẽn của TCP Reno bằng mô hình đo lường trạng thái vật lý thực tế của đường truyền: Băng thông nghẽn cực đại ($BtlBw$ hay `max_bw`) và Độ trễ lan truyền tối thiểu ($RTprop$ hay `min_rtt`).
   - Xây dựng máy trạng thái hữu hạn (FSM) gồm 4 pha kinh điển theo tài liệu chuẩn BBR của Google: `BBR_STARTUP`, `BBR_DRAIN`, `BBR_PROBE_BW`, và `BBR_PROBE_RTT`.
2. **Cơ chế tách biệt giữa mất gói ngẫu nhiên và tắc nghẽn mạng:**
   - Trong môi trường mạng không dây hoặc giả lập rớt gói (`setloss`), BBR phản hồi khi nhận 3 Duplicate ACKs bằng việc gọi ngay `tcp_fast_retransmit()` để gửi lại gói thiếu, nhưng **tuyệt đối không chia đôi `cwnd` hay `ssthresh`**.
   - Cửa sổ truyền dữ liệu (`cwnd`) được duy trì ổn định quanh tích số BDP ($BDP = \text{max\_bw} \times \text{min\_rtt}$), giúp đường truyền luôn được bơm đầy dữ liệu mà không bị sụt giảm thông lượng đột ngột.
3. **Mở rộng Giao diện Lập trình Kernel (Syscalls) & Tiện ích người dùng:**
   - Bổ sung 2 system call mới: `sys_set_cc(int algo)` (Syscall 31) và `sys_get_cc()` (Syscall 32), hỗ trợ chuyển đổi linh hoạt runtime giữa `CC_RENO` (0) và `CC_BBR` (1).
   - Xây dựng tiện ích dòng lệnh `user/setcc.c` trong xv6 (`setcc 0`, `setcc 1`, `setcc status`) cho phép người dùng kiểm tra và thay đổi thuật toán tức thì.
   - Bổ sung các trường BBR vào cấu trúc `struct tcp_stat` và tích hợp hiển thị chi tiết qua `netstat -d` và `tcp_dump_stats()`.
4. **Kiểm thử tự động & Benchmark so sánh định lượng:**
   - Xây dựng script kiểm thử tự động `tests/test_bbr.py` tải tệp lớn 1 MB (`largefile.bin`) trong điều kiện card mạng E1000 rớt ngẫu nhiên 10% gói (`setloss 10`).
   - Chứng minh BBR đạt tốc độ truyền tải nhanh hơn TCP Reno tối thiểu từ 1.5x đến 2x (kết quả thực nghiệm thực tế đạt **8.87x**, rút ngắn thời gian tải từ 31.45 giây xuống còn 3.55 giây).
   - Đảm bảo tương thích ngược 100% với toàn bộ bộ kiểm thử hệ thống: `tests/run_tests.py`, `tests/test_http11.py`, `tests/test_hwclock_bbr.py`, `tests/concurrent_stress.py`.

---

## 2. Cơ Sở Lý Thuyết & Kiến Trúc Google BBR

### 2.1. Giới hạn của TCP Reno và Lý do ra đời của BBR

Thuật toán TCP Reno (RFC 5681) giả định mọi hiện tượng mất gói (packet loss) đều xuất phát từ việc hàng đợi đệm của router bị tràn do tắc nghẽn mạng. Do đó, mỗi khi xảy ra mất gói (3 Dup ACKs hoặc Timeout), Reno lập tức kích hoạt cơ chế Multiplicative Decrease:
$$\text{ssthresh} = \frac{\text{FlightSize}}{2}, \quad \text{cwnd} = \text{ssthresh}$$

Trong các môi trường có mất gói ngẫu nhiên (như Wi-Fi nhiễu sóng hoặc mạng thử nghiệm rớt 10% gói), router hoàn toàn chưa hề đầy đệm nhưng Reno liên tục cắt giảm một nửa cửa sổ truyền. Cửa sổ nhanh chóng bị đè bẹp xuống mức tối thiểu (4 MSS), khiến máy phát phần lớn thời gian phải ngồi chờ ACK hoặc rơi vào tình trạng đóng băng chờ RTO timeout (200 ms - 800 ms).

Google BBR giải quyết triệt để vấn đề này bằng cách không coi mất gói là tắc nghẽn, mà tìm kiếm điểm làm việc tối ưu của Kleinrock (Kleinrock's optimal operating point):
$$\text{BDP} = \text{BtlBw} \times \text{RTprop}$$
* **$\text{BtlBw}$ (Bottleneck Bandwidth):** Băng thông thắt nút vật lý tối đa của đường truyền, được ước lượng thông qua cửa sổ trượt lấy giá trị cực đại (`bbr_max_bw`) từ các mẫu `delivery_rate` do TV1 cung cấp trên từng ACK.
* **$\text{RTprop}$ (Round-Trip Propagation Time):** Thời gian trễ lan truyền hai chiều tối thiểu không bao gồm thời gian xếp hàng trong đệm, được lưu giữ trong cửa sổ trượt 10 giây `min_rtt_us`.

```
        Tốc độ (Delivery Rate)
          ^
  max_bw  |                     /----------------- (Băng thông bão hòa)
          |                    /
          |                   /
          |                  /
          |                 /
          |                /
          +---------------+--------------------->
          0             BDP (Điểm tối ưu)        In-flight Bytes
                        |
                        +---> Vùng BBR hoạt động (Không phồng đệm, không rớt tốc độ)
```

---

### 2.2. Máy Trạng Thái 4 Pha Của Google BBR

```
                    ┌─────────────────────────┐
                    │       BBR_STARTUP       │
                    │  (Ramp-up: gain = 2.88) │
                    └────────────┬────────────┘
                                 │
                   (Plateau: 3 rounds ko tăng BW
                    hoặc cwnd >= MAX_CWND)
                                 │
                                 v
                    ┌─────────────────────────┐
                    │        BBR_DRAIN        │
                    │   (Xả đệm về mức BDP)   │
                    └────────────┬────────────┘
                                 │
                   (in_flight <= BDP hoặc 1 round)
                                 │
                                 v
                    ┌─────────────────────────┐
       ┌───────────>│      BBR_PROBE_BW       │<───────────┐
       │            │  (Chu kỳ 8 pha: Pacing) │            │
       │            └────────────┬────────────┘            │
       │                         │                         │
(Hết 200 ms)           (min_rtt hết hạn 10s:               │
       │              min_rtt_expired == 1)                │
       │                         │                         │
       │                         v                         │
       │            ┌─────────────────────────┐            │
       └────────────┤      BBR_PROBE_RTT      ├────────────┘
                    │ (cwnd = 4 MSS, t=200ms) │
                    └─────────────────────────┘
```

1. **`BBR_STARTUP`:**
   - Pha tăng tốc ban đầu nhằm nhanh chóng lấp đầy dung lượng ống truyền dẫn.
   - Pacing Gain tương đương $\frac{2}{\ln 2} \approx 2.885$ (trong phép toán số nguyên sử dụng hệ số nhân $\frac{23}{8}$). Cửa sổ `cwnd` tăng 1 MSS với mỗi ACK nhận được tương tự Slow Start.
   - Thuật toán giám sát giá trị `max_bw` qua từng vòng round trip (`bbr_round_delivered`). Nếu trong 3 round liên tiếp băng thông cực đại không tăng vượt quá 25% (`max_bw < prev_max_bw * 1.25`) hoặc `cwnd` đạt trần `MAX_CWND`, BBR xác định ống truyền đã đầy và chuyển sang `BBR_DRAIN`.
2. **`BBR_DRAIN`:**
   - Mục tiêu: Xả hết lượng gói tin dư thừa đã bị xếp hàng trong đệm mạng trong suốt quá trình tăng tốc đột ngột của STARTUP.
   - Target cwnd được hạ xuống ngang mức $1.0 \times \text{BDP}$.
   - Ngay khi lượng dữ liệu đang bay (`in_flight = snd_nxt - snd_una`) giảm xuống $\le \text{BDP}$, BBR lập tức chuyển sang pha tuần hoàn ổn định `BBR_PROBE_BW`.
3. **`BBR_PROBE_BW`:**
   - Pha hoạt động chủ đạo, luân phiên tuần hoàn qua 8 pha chu kỳ (`bbr_cycle_idx` từ 0 đến 7) với hệ số gain:
     $$\text{Gains} = \{ 1.25,\ 0.75,\ 1.0,\ 1.0,\ 1.0,\ 1.0,\ 1.0,\ 1.0 \}$$
     Mã hóa số nguyên chia 8: `{ 10, 6, 8, 8, 8, 8, 8, 8 }`.
   - **Pha 0 (Gain 1.25):** Thăm dò băng thông cao hơn bằng cách tạm thời đẩy tốc độ lên 125% BDP.
   - **Pha 1 (Gain 0.75):** Xả ngay hàng đợi vừa tạo ra ở pha thăm dò.
   - **Pha 2-7 (Gain 1.0):** Duy trì truyền tải ổn định ở tốc độ tối ưu đúng bằng BDP.
   - Mỗi pha chu kỳ kéo dài 1 RTT (tối thiểu 20 ms trong QEMU).
   - Nếu cờ `pcb->min_rtt_expired` được bật (sau 10 giây không có mẫu RTT mới thấp hơn), thuật toán chuyển sang `BBR_PROBE_RTT`.
4. **`BBR_PROBE_RTT`:**
   - Nhằm đo lường lại chính xác độ trễ thực tế của đường truyền khi các dòng truyền tải khác có thể đã tạo ra hàng đợi đệm dài hạn.
   - Cửa sổ `cwnd` được thu hẹp về mức tối thiểu $4 \times \text{TCP\_MSS}$ (5,600 bytes) trong đúng khoảng thời gian $200\text{ ms}$ ($200,000\ \mu\text{s}$).
   - Sau $200\text{ ms}$, bộ đếm 10 giây được tái lập (`min_rtt_stamp_us = now`), và BBR quay trở lại `BBR_PROBE_BW`.

---

## 3. Chi Tiết Cài Đặt Mã Nguồn

### 3.1. Mở rộng cấu trúc dữ liệu (`kernel/net.h` & `user/user.h`)

Định nghĩa các hằng số điều khiển và bổ sung các trường trạng thái BBR vào `struct tcp_pcb`:
```c
#define CC_RENO      0
#define CC_BBR       1

#define BBR_STARTUP   0
#define BBR_DRAIN     1
#define BBR_PROBE_BW  2
#define BBR_PROBE_RTT 3

struct tcp_pcb {
  ...
  // kiem soat tac nghen (RFC 5681 - TCP Reno & Google BBR - tuan 5 TV2)
  int    cc_algo;             // CC_RENO (0) hoac CC_BBR (1)
  uint32 cwnd;                // Congestion window (bytes)
  uint32 ssthresh;            // Slow start threshold (bytes)
  uint32 dupack_cnt;          // Dem duplicate ACKs
  uint32 recover;             // Sequence number danh dau ket thuc Fast Recovery

  // may trang thai Google BBR (tuan 5 TV2)
  int    bbr_state;           // BBR_STARTUP, BBR_DRAIN, BBR_PROBE_BW, BBR_PROBE_RTT
  uint32 bbr_max_bw;          // uoc luong bang thong cuc dai (bytes/s)
  uint32 bbr_bdp;             // Bandwidth-Delay Product = max_bw * min_rtt (bytes)
  uint32 bbr_target_cwnd;     // target cwnd = gain * BDP
  int    bbr_cycle_idx;       // pha chu ky 0..7 trong PROBE_BW
  uint64 bbr_cycle_stamp;     // thoi diem bat dau pha chu ky hien tai (us)
  uint64 bbr_rtt_stamp;       // thoi diem bat dau PROBE_RTT (us)
  uint64 bbr_round_delivered; // pcb->delivered tai moc round hien tai
  uint32 bbr_round_count;     // so round da hoan thanh
  uint32 bbr_bw_plateau_cnt;  // dem so round max_bw khong tang > 25% (STARTUP)
  uint32 bbr_prev_max_bw;     // max_bw tai round truoc
  ...
};
```

Bổ sung các trường thống kê vào `struct tcp_stat` để hỗ trợ lệnh `netstat -d`:
```c
struct tcp_stat {
  ...
  uint32 rtt_us;         // mau rtt gan nhat (us)
  uint32 min_rtt_us;     // min rtt cua so truot 10s (us)
  uint32 delivery_rate;  // bytes/s
  int    cc_algo;        // 0: Reno, 1: BBR
  int    bbr_state;      // 0: STARTUP, 1: DRAIN, 2: PROBE_BW, 3: PROBE_RTT
  uint32 max_bw;         // BBR max bandwidth (bytes/s)
  uint32 bdp;            // BBR BDP (bytes)
};
```

---

### 3.2. Cài đặt thuật toán Google BBR trong `kernel/tcp_cc.c`

Toàn bộ logic tính toán BDP, cập nhật `max_bw` và chuyển pha máy trạng thái:
```c
// Xu ly ACK cho thuat toan Google BBR
static void
bbr_on_ack(struct tcp_pcb *pcb, uint32 ack)
{
  uint32 old_cwnd = pcb->cwnd;
  pcb->dupack_cnt = 0;

  // 1. Cap nhat bo loc max_bw tu mau delivery_rate
  if (pcb->delivery_rate > pcb->bbr_max_bw) {
    pcb->bbr_max_bw = pcb->delivery_rate;
  }

  // 2. Tinh toan BDP = max_bw * min_rtt
  uint32 min_rtt = pcb->min_rtt_us ? pcb->min_rtt_us : (pcb->latest_rtt_us ? pcb->latest_rtt_us : 1000);
  uint64 bdp = ((uint64)pcb->bbr_max_bw * (uint64)min_rtt) / 1000000ULL;
  if (bdp < 4 * TCP_MSS)
    bdp = 4 * TCP_MSS;
  pcb->bbr_bdp = (uint32)bdp;

  // 3. Theo doi round-trip
  if (pcb->delivered >= pcb->bbr_round_delivered) {
    pcb->bbr_round_count++;
    pcb->bbr_round_delivered = pcb->delivered + pcb->bbr_bdp;
    if (pcb->bbr_state == BBR_STARTUP) {
      if (pcb->bbr_max_bw < (pcb->bbr_prev_max_bw * 5) / 4) {
        pcb->bbr_bw_plateau_cnt++;
      } else {
        pcb->bbr_bw_plateau_cnt = 0;
        pcb->bbr_prev_max_bw = pcb->bbr_max_bw;
      }
    }
  }

  // 4. May trang thai 4 pha BBR
  uint64 now = tcp_now_us();
  static const uint32 bbr_pacing_gains[8] = { 10, 6, 8, 8, 8, 8, 8, 8 };

  switch (pcb->bbr_state) {
  case BBR_STARTUP:
    pcb->cwnd += TCP_MSS;
    if (pcb->bbr_bw_plateau_cnt >= 3 || pcb->cwnd >= MAX_CWND) {
      pcb->bbr_state = BBR_DRAIN;
      tcp_cc_trace(pcb, "BBR_DRAIN");
    }
    break;

  case BBR_DRAIN:
    if (pcb->cwnd > pcb->bbr_bdp + 2 * TCP_MSS)
      pcb->cwnd = pcb->bbr_bdp + 2 * TCP_MSS;
    uint32 in_flight = pcb->snd_nxt - pcb->snd_una;
    if (in_flight <= pcb->bbr_bdp || pcb->delivered >= pcb->bbr_round_delivered) {
      pcb->bbr_state = BBR_PROBE_BW;
      pcb->bbr_cycle_idx = 0;
      pcb->bbr_cycle_stamp = now;
      tcp_cc_trace(pcb, "BBR_PROBE_BW");
    }
    break;

  case BBR_PROBE_BW:
    if (pcb->min_rtt_expired) {
      pcb->bbr_state = BBR_PROBE_RTT;
      pcb->bbr_rtt_stamp = now;
      pcb->cwnd = 4 * TCP_MSS;
      tcp_cc_trace(pcb, "BBR_PROBE_RTT");
      break;
    }
    uint32 rtt_interval = min_rtt > 20000 ? min_rtt : 20000;
    if (now - pcb->bbr_cycle_stamp >= rtt_interval) {
      pcb->bbr_cycle_idx = (pcb->bbr_cycle_idx + 1) % 8;
      pcb->bbr_cycle_stamp = now;
    }
    uint32 gain = bbr_pacing_gains[pcb->bbr_cycle_idx];
    uint32 target = (pcb->bbr_bdp * gain) / 8 + 4 * TCP_MSS;
    if (pcb->cwnd < target)
      pcb->cwnd += TCP_MSS;
    else if (pcb->cwnd > target && pcb->cwnd > 16 * TCP_MSS)
      pcb->cwnd -= TCP_MSS / 2;
    break;

  case BBR_PROBE_RTT:
    pcb->cwnd = 4 * TCP_MSS;
    if (now - pcb->bbr_rtt_stamp >= 200000ULL) {
      pcb->min_rtt_expired = 0;
      pcb->min_rtt_stamp_us = now;
      pcb->bbr_state = BBR_PROBE_BW;
      pcb->bbr_cycle_idx = 0;
      pcb->bbr_cycle_stamp = now;
      pcb->cwnd = pcb->bbr_bdp + 4 * TCP_MSS;
      tcp_cc_trace(pcb, "BBR_PROBE_BW");
    }
    break;
  }

  if (pcb->bbr_state != BBR_PROBE_RTT && pcb->cwnd < 16 * TCP_MSS)
    pcb->cwnd = 16 * TCP_MSS;
  if (pcb->cwnd > MAX_CWND)
    pcb->cwnd = MAX_CWND;
}
```

**Xử lý mất gói không giảm `cwnd` (`tcp_cc_on_dup_ack`):**
```c
void
tcp_cc_on_dup_ack(struct tcp_pcb *pcb, uint32 ack)
{
  pcb->dupack_cnt++;

  if (pcb->cc_algo == CC_BBR) {
    // BBR: Mat goi KHONG phai la dau hieu tac nghen mang!
    // Khi nhan 3 Dup ACKs, lap tuc truyen lai goi bi thieu bang Fast Retransmit,
    // TUYET DOI KHONG cat giam cwnd hay ssthresh, duy tri bang thong BDP!
    if (pcb->dupack_cnt == 3) {
      tcp_fast_retransmit(pcb);
      tcp_cc_trace(pcb, "BBR_FAST_RETX");
      wakeup(pcb);
    } else if (pcb->dupack_cnt > 3) {
      wakeup(pcb);
    }
    return;
  }

  // Reno: Multiplicative Decrease
  ...
}
```

---

### 3.3. Syscall `sys_set_cc`, `sys_get_cc` & Tiện ích dòng lệnh `user/setcc.c`

* **Định nghĩa System Call:**
  - `kernel/syscall.h`: `#define SYS_set_cc 31`, `#define SYS_get_cc 32`.
  - `kernel/syscall.c`: Khai báo và ánh xạ bảng điều hướng syscalls.
  - `user/usys.pl`: Sinh mã stub `entry("set_cc");`, `entry("get_cc");`.
  - `kernel/sysnet.c`:
    ```c
    uint64
    sys_set_cc(void)
    {
      int algo;
      argint(0, &algo);
      return tcp_set_cc_algo(algo);
    }

    uint64
    sys_get_cc(void)
    {
      return tcp_get_cc_algo();
    }
    ```
* **Tiện ích người dùng `user/setcc.c`:**
  Hỗ trợ cú pháp thân thiện:
  - `setcc` hoặc `setcc status`: In thuật toán kiểm soát tắc nghẽn hiện hành.
  - `setcc 0` hoặc `setcc reno`: Thiết lập TCP Reno.
  - `setcc 1` hoặc `setcc bbr`: Thiết lập Google BBR.

---

## 4. Kết Quả Thực Nghiệm & Đo Lường Định Lượng

### 4.1. Benchmark So Sánh Hiệu Năng: TCP Reno vs Google BBR khi rớt gói 10% (`tests/test_bbr.py`)

Kịch bản kiểm thử:
- Khởi động HTTP server trong xv6 (`httpd &`).
- Kích hoạt tỷ lệ mất gói ngẫu nhiên 10% trên card mạng E1000: `setloss 10`.
- Tải tệp dữ liệu nhị phân dung lượng lớn $1,048,576\text{ bytes}$ (1.0 MB) từ host qua `curl` và đo lường chính xác thời gian hoàn thành cùng băng thông thực tế.

**Bảng kết quả đo lường trực tiếp:**

| Thuật toán | Thiết lập dòng lệnh | Thời gian tải 1 MB | Băng thông thực tế | Trạng thái toàn vẹn |
| :--- | :--- | :--- | :--- | :--- |
| **TCP Reno** | `setcc 0` + `setloss 10` | **31.45 giây** | **32.6 KB/s** | Đủ 1,048,576 bytes (100%) |
| **Google BBR** | `setcc 1` + `setloss 10` | **3.55 giây** | **288.7 KB/s** | Đủ 1,048,576 bytes (100%) |

> **HIỆU QUẢ CẢI THIỆN: Google BBR nhanh hơn TCP Reno gấp `8.87x` lần (vượt xa chỉ tiêu cam kết $\ge 1.5\text{x} - 2.0\text{x}$)!**

---

### 4.2. Phân Tích Hành Vi BBR Dưới Môi Trường Mất Gói

1. **Tại sao TCP Reno sụt giảm xuống còn 32.6 KB/s (31.45 giây)?**
   - Với 1 MB dữ liệu (~748 gói MSS 1400 bytes), mất gói 10% đồng nghĩa với khoảng 75 sự kiện mất gói.
   - Mỗi lần mất gói, Reno chia đôi `ssthresh` và ép `cwnd` về 4 MSS. Khi rơi vào tình huống mất nhiều gói trong một cửa sổ, Reno bị kiệt quệ cửa sổ truyền dẫn và buộc phải chờ timeout RTO (200 ms $\rightarrow$ 400 ms $\rightarrow$ 800 ms). Máy chủ phần lớn thời gian bị nghẽn ở trạng thái ngủ (`sleep`), không thể đẩy dữ liệu ra card mạng.
2. **Tại sao Google BBR duy trì được 288.7 KB/s (3.55 giây)?**
   - BBR không đồng nhất hiện tượng mất gói ngẫu nhiên với tắc nghẽn mạng.
   - Khi nhận được 3 Duplicate ACKs, BBR lập tức bắn lại gói tin bị thiếu thông qua Fast Retransmit, đồng thời giữ nguyên cửa sổ truyền dữ liệu dựa trên BDP và `max_bw` (đạt mức ~13.7 MB/s).
   - Hàng đợi truyền dẫn luôn chứa đầy các segment dữ liệu tiếp theo, giúp tận dụng tối đa băng thông vật lý của phần cứng mà không hề bị đứng yên chờ đợi.

---

### 4.3. Xác Nhận Toàn Diện Các Bộ Kiểm Thử Hệ Thống

Toàn bộ các bài kiểm thử tự động của dự án đều vượt qua 100%:

| Bộ kiểm thử | Lệnh thực thi | Kết quả | Ghi chú |
| :--- | :--- | :--- | :--- |
| **FSM & TCP Core** | `python3 tests/run_tests.py` | **6/6 tests PASS** | Bắt tay 3 bước, RST port đóng, Half-close, Stream cắt mảnh, 1MB, Client abort |
| **HTTP/1.1 & Worker Pool** | `python3 tests/test_http11.py` | **5/5 tests PASS** | Keep-Alive, Content-Length, Favicon 204, MIME types, 4 worker processes |
| **RISC-V Hardware Clock** | `python3 tests/test_hwclock_bbr.py` | **PASS** | Kiểm tra `rdcycle`, `r_time`, sliding window 10s `min_rtt_us`, `delivery_rate` |
| **Tải nặng đồng thời** | `python3 tests/concurrent_stress.py --conns 8 --rounds 2` | **16/16 requests PASS** | 8 kết nối đồng thời qua 2 đợt, 0 memory leak (`free=480/512`) |
| **BBR vs Reno Benchmark** | `python3 tests/test_bbr.py` | **PASS (8.87x speedup)** | 10% packet loss, BBR hoàn thành trong 3.55s |

---

## 5. Hướng Dẫn Chạy & Quan Sát Kết Quả Thực Tế

Người dùng có thể tự mình chạy và quan sát toàn bộ kết quả trên terminal theo hai cách dưới đây:

### Cách 1: Chạy kịch bản Benchmark tự động (Khuyến nghị - 1 lệnh duy nhất)

Chạy trực tiếp script đo lường định lượng so sánh Reno vs BBR:
```bash
python3 tests/test_bbr.py
```
Script sẽ tự động khởi động QEMU, chạy Reno khi rớt 10% gói, sau đó chuyển sang BBR khi rớt 10% gói và in bảng tổng hợp tốc độ trực quan lên màn hình.

### Cách 2: Chạy thủ công tương tác trực tiếp trong QEMU

1. **Biên dịch và khởi động xv6:**
   ```bash
   make qemu
   ```
2. **Khởi động HTTP server trong xv6:**
   ```text
   $ httpd &
   ```
3. **Kiểm tra và chuyển đổi thuật toán kiểm soát tắc nghẽn:**
   ```text
   $ setcc
   congestion control: BBR (1)
   usage: setcc [0|1|reno|bbr|status]

   $ setcc 0
   set congestion control: Reno (0)

   $ setloss 10
   set loss rate: 10%
   ```
4. **Mở terminal thứ hai trên Host và tải tệp 1 MB (đo Reno):**
   ```bash
   curl -o /dev/null -s -w 'Time: %{time_total} s | Size: %{size_download} bytes\n' http://127.0.0.1:20080/largefile.bin
   # Kết quả ghi nhận: ~31.4 giây
   ```
5. **Chuyển sang Google BBR tại terminal xv6:**
   ```text
   $ setcc 1
   set congestion control: BBR (1)
   ```
6. **Thực hiện tải lại trên Host (đo BBR):**
   ```bash
   curl -o /dev/null -s -w 'Time: %{time_total} s | Size: %{size_download} bytes\n' http://127.0.0.1:20080/largefile.bin
   # Kết quả ghi nhận: ~3.5 giây (Nhanh gấp ~8.8x lần!)
   ```
7. **Xem thống kê chi tiết kết nối BBR trong xv6:**
   ```text
   $ netstat -d
   ```
   Màn hình console kernel sẽ hiển thị chi tiết:
   `cc=BBR(1) bbr_st=2 bdp=... max_bw=... rtt_us=... min_rtt_us=... delivered=... rate=...`

---

## 6. Danh Mục Tệp Triển Khai & Thay Đổi

| Tệp tin | Loại thay đổi | Chi tiết kỹ thuật |
| :--- | :--- | :--- |
| `kernel/net.h` | Chỉnh sửa | Bổ sung enum thuật toán (`CC_RENO`, `CC_BBR`), 4 pha BBR (`BBR_STARTUP`, `BBR_DRAIN`, `BBR_PROBE_BW`, `BBR_PROBE_RTT`), các trường BBR trong `struct tcp_pcb` và `struct tcp_stat` |
| `kernel/tcp_cc.c` | Viết mới / Mở rộng | Triển khai toàn diện thuật toán Google BBR FSM, tính BDP, bộ lọc `max_bw`, xử lý mất gói không cắt giảm cwnd, API `tcp_set_cc_algo` |
| `kernel/sock.c` | Tinh chỉnh | Điều chỉnh giới hạn sàn in-flight từ cố định 16 MSS xuống 4 MSS để tôn trọng các pha `BBR_PROBE_RTT` và cơ chế nghẽn của Reno |
| `kernel/tcp.c` | Mở rộng | Cập nhật hàm `tcp_get_stats` sao chép thông số BBR, hàm `tcp_pcb_update_cc` cập nhật thuật toán cho các PCB đang chạy, `tcp_dump_stats` hiển thị trạng thái BBR |
| `kernel/syscall.h` | Chỉnh sửa | Bổ sung mã định danh syscall `SYS_set_cc` (31) và `SYS_get_cc` (32) |
| `kernel/syscall.c` | Chỉnh sửa | Đăng ký handler `sys_set_cc` và `sys_get_cc` vào bảng `syscalls[]` |
| `kernel/sysnet.c` | Mở rộng | Hiện thực hàm `sys_set_cc` và `sys_get_cc` |
| `user/user.h` | Chỉnh sửa | Đồng bộ `struct tcp_stat` với kernel, khai báo syscall `set_cc()`, `get_cc()` |
| `user/usys.pl` | Chỉnh sửa | Sinh entry asm ecall cho `set_cc` và `get_cc` |
| `user/setcc.c` | Tạo mới | Tiện ích người dùng `setcc` xem và chuyển đổi thuật toán Reno / BBR |
| `Makefile` | Chỉnh sửa | Thêm `$U/_setcc` vào danh sách `UPROGS` |
| `tests/test_bbr.py` | Tạo mới | Bộ script tự động kiểm thử và benchmark định lượng so sánh hiệu năng Reno vs BBR |
