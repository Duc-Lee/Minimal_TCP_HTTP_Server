# <p align="center">PROGRESS REPORT</p>

**Course:** Operating Systems - 20252  
**Project No:** 26130  
**Project title:** Minimal TCP and an HTTP server  
**Design thinking page:** Figma  
**Task management page:** Trello  
**Members:**  
1. Phùng Huy Hoàng - 20233414  
2. Nguyễn Vũ Hiệp - 20233392  
3. Lê Anh Đức - 20233324  
**Reported working period:**  
from: 15/11/2026 to: 21/11/2026 (Tuần 5)

---

## Summary of latest progress

Sau khi hoàn thiện máy chủ HTTP/1.1 và hàng đợi FIFO ở Tuần 4, hệ thống còn hai nút thắt lớn:
* TCP Reno xem mất gói ngẫu nhiên là tắc nghẽn, liên tục chia đôi `cwnd` khiến mạng ảo giả lập rớt gói 10% bị sụt giảm thông lượng nghiêm trọng.
* Phép đo RTT thô theo tick (100 ms) không đủ chính xác để tính toán băng thông thực tế.
* Quá trình phục vụ file tĩnh của `httpd` tốn nhiều lần copy bộ nhớ (Disk $\rightarrow$ Kernel $\rightarrow$ User $\rightarrow$ Kernel $\rightarrow$ Card mạng), tiêu tốn chu kỳ CPU.

Trong Tuần 5, nhóm đã hoàn thành:
1. **Phùng Huy Hoàng (TV2):** Hiện thực thuật toán điều khiển tắc nghẽn Google BBR với 4 pha (`STARTUP`, `DRAIN`, `PROBE_BW`, `PROBE_RTT`). Bổ sung syscall `set_cc`/`get_cc` và lệnh `setcc`. Khi mất gói 10%, BBR tải file 1 MB nhanh gấp **6.1x** so với Reno.
2. **Nguyễn Vũ Hiệp (TV1):** Mở quyền thanh ghi bộ đếm phần cứng RISC-V (`rdcycle`, `r_time`), viết module `hwclock` đo RTT độ phân giải microsecond, cửa sổ trượt 10 giây `min_rtt` và bộ ước lượng `delivery_rate` phục vụ BBR.
3. **Lê Anh Đức (TV3):** Hiện thực syscall Zero-Copy `sys_sendfile` truyền trực tiếp từ buffer cache đĩa sang `mbuf`, giảm ~35% chu kỳ CPU. Tích hợp `sendfile` vào `httpd`, viết dashboard trực quan `tcptrace` và tái cấu trúc toàn bộ thư mục `tests/` theo module.

Tiến độ: Đạt khoảng **95%** kế hoạch 6 tuần.

---

## Review of technical activities

* **Activity 1 — Google BBR & Syscall chuyển đổi thuật toán (Hoàng):**  
  Cài đặt mô hình tắc nghẽn Kleinrock ($BDP = \text{max\_bw} \times \text{min\_rtt}$). Khi nhận 3 Dup ACK, BBR kích hoạt Fast Retransmit nhưng không cắt giảm `cwnd`. Thêm syscall `sys_set_cc(algo)` (31), `sys_get_cc()` (32) và tiện ích `user/setcc.c`.

* **Activity 2 — Bộ đếm phần cứng RISC-V & Đo RTT Microsecond (Hiệp):**  
  Cấu hình `mcounteren` và `scounteren` mở quyền đọc `cycle`/`time` cho S-mode và U-mode. Xây dựng `kernel/hwclock.c` tự động hiệu chuẩn xung nhịp CPU theo CLINT `mtime` (10 MHz). Đo RTT microsecond theo Quy tắc Karn, quản lý cửa sổ trượt 10s `min_rtt` và tính `delivery_rate`.

* **Activity 3 — Zero-Copy `sendfile`, Dashboard `tcptrace` & Refactor Test (Đức):**  
  Syscall `sendfile(out_fd, in_fd, offset, count)` (33) đọc trực tiếp từ inode buffer cache vào payload `mbuf`, bỏ qua sao chép qua user space. Tích hợp `sendfile` vào `httpd` (có fallback). Viết công cụ `tcptrace` hiển thị trực quan ANSI/ASCII bar chart (`cwnd`, `ssthresh`, `rto`, pha BBR). Tái cấu trúc thư mục `tests/` thành `tcp/`, `http/`, `common/`.

---

## Personal contribution

* **Phùng Huy Hoàng - 20233414:** Thuật toán Google BBR, syscall `set_cc`/`get_cc`, tiện ích `setcc`, mở rộng `netstat -d`, benchmark BBR vs Reno.  
  *Output:* `kernel/tcp_cc.c`, `kernel/sysnet.c`, `user/setcc.c`, `tests/tcp/test_bbr.py`.

* **Nguyễn Vũ Hiệp - 20233392:** Phân quyền CSR bộ đếm (`mcounteren`/`scounteren`), module `hwclock.c`, đo RTT microsecond, cửa sổ trượt 10s `min_rtt`, tính `delivery_rate`, tiện ích `user/hwclock.c`.  
  *Output:* `kernel/hwclock.c`, `kernel/tcp_timer.c`, `kernel/riscv.h`, `user/hwclock.c`, `tests/tcp/test_hwclock.py`.

* **Lê Anh Đức - 20233324:** Syscall Zero-Copy `sys_sendfile`, đo đạc chu kỳ CPU `rdcycle`, tích hợp `sendfile` vào `httpd`, xây dựng dashboard `user/tcptrace.c`, chuẩn hóa cấu trúc `tests/`.  
  *Output:* `kernel/sendfile.c`, `user/httpd.c`, `user/tcptrace.c`, `tests/run_tests.py`, `tests/http/test_sendfile.py`.

---

## Developed code review

### a. Algorithmic development
* **Google BBR Congestion Control (Hoàng):** Thay thế cơ chế AIMD của Reno bằng mô hình Kleinrock ($BDP = \text{max\_bw} \times \text{min\_rtt}$); máy trạng thái 4 pha (`STARTUP`, `DRAIN`, `PROBE_BW`, `PROBE_RTT`). Nhận 3 Dup ACKs gọi Fast Retransmit nhưng không cắt giảm `cwnd`, tránh sụt giảm thông lượng khi mạng rớt gói ngẫu nhiên.
* **Microsecond RTT & Cửa sổ trượt 10s `min_rtt` (Hiệp):** Mở thanh ghi CSR phần cứng `cycle`/`time` qua `mcounteren`/`scounteren`; đo RTT thời gian thực microsecond (thay vì 100ms/tick); bộ lọc cửa sổ trượt 10s `min_rtt` và ước lượng `delivery_rate` cung cấp thông số đầu vào chuẩn xác cho BBR.
* **Zero-Copy Data Transfer (Đức):** Syscall `sys_sendfile` đọc trực tiếp từ Buffer Cache của filesystem (`readi` với `user_dst = 0`) vào payload `mbuf`, truyền thẳng xuống card mạng E1000 qua `tcp_output()`, giảm từ 4 lần copy xuống còn 2 lần (DMA) + 1 lần CPU copy nội bộ kernel.
* **Live Observability Engine (Đức):** Hệ thống giám sát thời gian thực không xâm lấn; định vị con trỏ ANSI escape `\033[2J\033[H`, chuyển đổi thông số raw từ `netstat()` thành biểu đồ thanh ASCII (`cwnd`, `ssthresh`, `rto`, pha BBR) làm mới chu kỳ 500ms.

### b. Source code enhanced and/or revised
* `kernel/tcp_cc.c`, `kernel/sysnet.c`, `user/setcc.c`: Thuật toán BBR, syscall `sys_set_cc` (31), `sys_get_cc` (32), tiện ích `setcc`.
* `kernel/hwclock.c`, `kernel/tcp_timer.c`, `kernel/riscv.h`, `user/hwclock.c`: Khởi tạo và hiệu chuẩn clock CLINT 10 MHz, đo RTT us theo Karn, cửa sổ 10s `min_rtt`, tiện ích `hwclock`.
* `kernel/sendfile.c`, `kernel/syscall.c`, `kernel/syscall.h`, `user/user.h`, `user/usys.pl`: Hiện thực Syscall 33 `sys_sendfile` kết nối buffer cache đĩa và socket mbuf.
* `user/httpd.c`: Phục vụ file tĩnh qua `sendfile` với cơ chế fallback về `read/write`.
* `user/tcptrace.c`: Dashboard trực quan hiển thị số liệu TCP/BBR thời gian thực.
* `tests/run_tests.py`, `tests/tcp/`, `tests/http/`, `tests/common/`: Tái cấu trúc bộ test theo module, runner trung tâm, hỗ trợ tham số `--host`, `--port` linh hoạt.

### c. Testing result
* **BBR vs Reno (`tests/tcp/test_bbr.py`):** Khi mất gói 10%, BBR tải file 1 MB trong 4.05s (252.4 KB/s) so với Reno 24.72s (41.4 KB/s) $\rightarrow$ Tăng tốc **6.10x** (vượt chỉ tiêu $\ge 1.5x$).
* **Hardware Counter & RTT (`tests/tcp/test_hwclock.py`):** Đồng hồ `cycle` và `time` tăng đơn điệu 100% qua 100,000 mẫu; đo RTT thực tế `rtt_us = 157 µs`, `min_rtt_us = 46 µs`, `rate = 8708 KB/s`.
* **Zero-Copy sendfile (`tests/http/test_sendfile.py`):** Tải 1 MB trong 0.50s, mã băm SHA-256 trùng khớp 100% với file gốc; tiết kiệm **35.4%** chu kỳ CPU qua `rdcycle` (vượt chỉ tiêu $\ge 25\% - 40\%$).
* **Kiểm thử hồi quy (`tests/run_tests.py core`):** 100% pass toàn bộ test FSM (6/6), HTTP/1.1 keep-alive (5/5), và sendfile (3/3).

---

## TODO

### a) List of remaining issues
* Cần kiểm thử stress test đồng thời với nhiều worker tải các file dung lượng khác nhau để đánh giá khả năng chịu tải cực hạn của zero-copy `sendfile`.
* Cần chuẩn bị kịch bản demo trực tiếp cho buổi bảo vệ đồ án: chạy đồng thời máy chủ HTTP, tải file qua BBR và bật dashboard `tcptrace` trên màn hình.

### b) List of tasks to carry out in the next period (Tuần 6 - Tuần cuối tổng kết & bảo vệ)
* **Đức:** Hoàn thiện kịch bản demo hệ thống kết hợp `httpd`, `sendfile` và `tcptrace`; chuẩn bị slide thuyết trình phần Web Server & Zero-Copy.
* **Hoàng:** Vẽ biểu đồ so sánh chi tiết hiệu năng BBR vs Reno ở các tỷ lệ mất gói khác nhau (0%, 5%, 10%); hoàn thiện báo cáo phân tích thuật toán BBR.
* **Hiệp:** Tổng kết số liệu đo đạc xung nhịp phần cứng RISC-V và RTT microsecond; đóng gói mã nguồn hoàn chỉnh và tài liệu kỹ thuật của nhóm.

### c) Milestone
Kết thúc Tuần 6: Hoàn thành toàn bộ báo cáo tổng kết đồ án, slide bảo vệ, video demo hệ thống và sẵn sàng bảo vệ đồ án môn học.

---

## Minh chứng kết quả thực nghiệm

### 1. Phùng Huy Hoàng (TV2) — Thuật toán Google BBR vs Reno (Mất gói 10%)

* **Benchmark so sánh tự động (`tests/tcp/test_bbr.py`):**
  Trong điều kiện rớt gói 10% (`setloss 10`), Google BBR tải file 1 MB chỉ mất **4.05 giây** (252.4 KB/s), trong khi Reno mất **24.72 giây** (41.4 KB/s) do bị phạt giảm `cwnd`. BBR nhanh hơn Reno gấp **6.10x** (vượt chỉ tiêu $\ge 1.5x$).
  
  ![Benchmark BBR vs Reno loss 10%](image_doang/image.png)

* **Quan sát trạng thái BBR trong kernel và curl tải file thực tế:**
  Bên trái là console kernel hiển thị các pha chuyển dịch của BBR (`BBR_DRAIN`, `BBR_FAST_RETX`, `BBR_PROBE_BW`), bên phải là kiểm tra curl tải file 1 MB trực tiếp (Reno: 24.68s vs BBR: 3.67s).
  
  ![Kernel log BBR va curl download](<image_doang/image copy.png>)

---

### 2. Nguyễn Vũ Hiệp (TV1) — Bộ đếm phần cứng & Đo RTT Microsecond

* **Kiểm thử bộ đếm phần cứng và RTT thực tế (`tests/tcp/test_hwclock.py`):**
  - Hiệu chuẩn CLINT 10 MHz: 10 tick/µs, CPU đo được ~3109.6 cycle/µs.
  - Kiểm tra 100,000 mẫu: đồng hồ `time` và `cycle` tăng đơn điệu 100% (0 lần lùi).
  - Kết nối truyền dữ liệu thực tế đo được độ trễ chính xác: `rtt_us=157`, `min_rtt_us=46`, `rate=8708 KB/s`.
  
  ![Kiem tra hwclock va do RTT us](image_hiep/image.png)

---

### 3. Lê Anh Đức (TV3) — Zero-Copy `sendfile`, Tiết kiệm CPU & Dashboard `tcptrace`

* **Kiểm thử toàn vẹn Zero-Copy `sendfile` (`tests/http/test_sendfile.py`):**
  File 1 MB truyền xong trong **0.50 giây**, mã băm SHA-256 trùng khớp 100% với file gốc trên đĩa:
  ```text
  $ python3 tests/http/test_sendfile.py
  test_sendfile_small: ok
  test_sendfile_1mb: ok (0.50s)
  test_sendfile_keepalive: ok
  all tests passed
  ```
  ![Kiem thu Zero-Copy sendfile va regression test](image_duc/image.png)

* **Đánh giá mức độ tiết kiệm chu kỳ CPU qua `rdcycle`:**

  | Tiêu chí | Truyền thống (`read` + `write`) | Zero-Copy (`sendfile`) | Cải thiện |
  | :--- | :--- | :--- | :--- |
  | **Chu kỳ CPU (`rdcycle`)** | ~48,200,000 | ~31,100,000 | **Giảm 35.4%** |
  | **Số lần gọi syscall** | 512 lần (`256 read + 256 write`) | 1 lần | **Giảm 99.8%** |
  | **Số lần copy bộ nhớ** | 2 lần (Kernel $\leftrightarrow$ User) | 0 lần qua User | **Loại bỏ hoàn toàn copy user-space** |

* **Dashboard quan sát kết nối trực tiếp `tcptrace`:**
  Hiển thị trực quan qua mã ANSI và thanh biểu đồ ASCII trên console xv6:
  ```text
  tcptrace (frame 1)

  conn 1: 0.0.0.0:80 -> 10.0.2.2:54321 [ESTABLISHED]
    cc: BBR (PROBE_BW) | bw: 612 KB/s | bdp: 58240 B
    rtt: 95 us | min_rtt: 95 us | rate: 612 KB/s
    inflight: 1400 B | dupack: 0 | backoff: 0
    cwnd     [####################..........] 44800 B
    ssthresh [##############################] 65535 B
    rto      [##............................] 200 ms
  ```
  ![Dashboard quan sat truc quan tcptrace](<image_duc/image copy.png>)

* **Kiểm thử tích hợp runner trung tâm (`tests/run_tests.py`):**
  ```text
  $ python3 tests/run_tests.py core
  >>> tests/tcp/test_state.py: ok (6/6 pass)
  >>> tests/http/test_http11.py: ok (5/5 pass)
  >>> tests/http/test_sendfile.py: ok (3/3 pass)
  tat ca test deu pass
  ```
