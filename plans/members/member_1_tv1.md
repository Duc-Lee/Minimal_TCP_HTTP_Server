# KẾ HOẠCH HÀNH ĐỘNG CHI TIẾT - THÀNH VIÊN 1 (TV1)
## Chuyên môn: Hạ Tầng Mạng, Bộ Đệm Mbuf, Driver E1000 & RTO Timer

---

## 1. Thông Tin Thành Viên & Phạm Vi Kỹ Thuật

* **Định hướng kỹ thuật chính:** Hạ Tầng Mạng, Bộ Đệm Mbuf, Driver E1000 & RTO Timer (phối hợp linh hoạt cùng nhóm).
* **Tỷ trọng đóng góp:** 33.3% khối lượng toàn bộ dự án.
* **Danh mục mã nguồn phụ trách chính:**
  * `kernel/mbuf.c`: Quản lý cấp phát, thu hồi và đồng bộ hóa hàng đợi bộ đệm `mbuf`.
  * `kernel/e1000.c`: Driver điều khiển card mạng Intel 82540EM (E1000) và bộ giả lập mất gói (Packet Loss Simulator).
  * `kernel/eth.c`: Xử lý khung tin Ethernet (L2).
  * `kernel/arp.c`: Giao thức ánh xạ địa chỉ ARP, bảng ARP Cache và cơ chế lão hóa (Cache Aging).
  * `kernel/ip.c`: Giao thức mạng IP (L3), kiểm tra định tuyến và gói tin IPv4.
  * `kernel/tcp_timer.c`: Bộ đếm thời gian RTO (Jacobson/Karels Algorithm) và lịch quét định kỳ TCP tick.
  * `kernel/checksum.c`: Tính toán Internet Checksum (RFC 1071) tăng tốc bằng thuật toán cộng dồn 32-bit.
  * `user/setloss.c`: Tiện ích CLI điều khiển cấu hình tỉ lệ rơi gói từ giao diện người dùng.

---

## 2. Lộ Trình 6 Tuần Chi Tiết Của Thành Viên 1

### TUẦN 1: Khử Lỗi Deadlock Bộ Đệm Mbuf, Vá Rò Rỉ ARP & Chuẩn Hóa RTO Timer
> **Trạng thái:** ✅ Đã hoàn thành xuất sắc (Đã commit & push tại `f30886a`, `74a11f9`).

* **Mục tiêu kỹ thuật:** Giải quyết triệt để các lỗi sập hệ thống (deadlock) và rò rỉ bộ nhớ khi truyền tải dữ liệu dung lượng lớn (1 MB).
* **Nhiệm vụ cụ thể:**
  1. **Khử Deadlock trong `kernel/mbuf.c`:**
     - Triển khai hàm `mbufalloc_wait()`: Khi hết bộ đệm trong `mbuf_pool`, tiến trình gọi hàm sẽ chuyển sang trạng thái ngủ (`sleep(&mbuf_pool, &mbuf_lock)`) thay vì trả về con trỏ `0` gây sập socket.
     - Bổ sung `wakeup(&mbuf_pool)` trong hàm `mbuffree()` ngay khi có buffer được giải phóng về pool.
  2. **Vá lỗi Header Headroom:**
     - Loại bỏ kỹ thuật ghi đè `*(uint32 *)(clone->buf) = seq_end` gây rủi ro làm hỏng dữ liệu.
     - Khai báo bổ sung trường `uint32 seq_end;` trực tiếp trong định nghĩa `struct mbuf` tại `kernel/net.h`.
  3. **Khử rò rỉ bộ nhớ trong `kernel/arp.c`:**
     - Trong `arp_resolve()`, nếu bảng ARP Cache đã đầy (không còn slot trống), phải gọi `mbuffree(pending_m)` ngay lập tức để tránh thất thoát gói tin chưa gửi.
  4. **Hiệu chuẩn bộ đếm thời gian RTO (`kernel/tcp_timer.c`):**
     - Phát hiện xung nhịp phần cứng của xv6 chạy ở tần số **10 Hz (1 tick = 100ms)** chứ không phải 100 Hz.
     - Viết lại macro tính toán tick: `#define MS_TO_TICKS(ms) (((ms) + 99) / 100)`.
     - Đưa thời gian timeout từ 2.0 giây về đúng 200ms, giúp tốc độ tải 1 MB rút ngắn từ 38s xuống chỉ còn 4s.

---

### TUẦN 2: Bộ Giả Lập Mất Gói Động (Configurable Loss Simulator) & ARP Aging
> **Trạng thái:** 🚀 Đang tiến hành / Sẵn sàng bàn giao.

* **Mục tiêu kỹ thuật:** Cho phép người dùng hoặc kịch bản kiểm thử điều khiển tỷ lệ mất gói linh hoạt mà không cần phải can thiệp hay biên dịch lại nhân hệ điều hành.
* **Nhiệm vụ cụ thể:**
  1. **Hiện thực Loss Simulator trong `kernel/e1000.c`:**
     - Chuyển logic drop gói cố định thành biến toàn cục `int packet_loss_rate` (từ 0% đến 100%).
     - Cài đặt hai hàm kernel API: `e1000_set_loss_rate(int rate)` và `e1000_get_loss_rate(void)`.
  2. **Thêm các System Calls mới:**
     - Khai báo mã syscall trong `kernel/syscall.h`: `SYS_set_loss_rate` (28) và `SYS_get_loss_rate` (29).
     - Hiện thực hàm xử lý kernel trong `kernel/sysnet.c`: `sys_set_loss_rate()` và `sys_get_loss_rate()`.
     - Đăng ký wrapper ở userspace trong `user/user.h` và `user/usys.pl`.
  3. **Xây dựng công cụ dòng lệnh `user/setloss.c`:**
     - Hỗ trợ cú pháp: `setloss <rate_percent>` (ví dụ: `setloss 10`, `setloss 0`) và `setloss status`.
  4. **Triển khai cơ chế ARP Cache Aging (`kernel/arp.c`):**
     - Thêm trường `uint last_seen_tick;` vào cấu trúc bản ghi ARP.
     - Quét định kỳ trong timer interrupt: tự động giải phóng các bản ghi ARP không hoạt động quá 60 giây để tránh lưu trữ địa chỉ MAC lỗi thời.

---

### TUẦN 3: Tinh Chỉnh Đo Đạc RTT Chuẩn Quy Tắc Karn & Giới Hạn Retransmission
* **Mục tiêu kỹ thuật:** Cung cấp thông số RTT và RTO chính xác cho thuật toán kiểm soát tắc nghẽn của TV2, không để sai lệch do mạng giật/lag.
* **Nhiệm vụ cụ thể:**
  1. **Triển khai Quy tắc Karn (Karn's Algorithm):**
     - Đánh dấu cờ `retransmitted` trong `struct mbuf`.
     - Nếu một segment bị mất và phải gửi lại, tuyệt đối **không** dùng thời gian nhận ACK của segment đó để cập nhật `srtt` và `rttvar` (tránh hiện tượng RTT Retransmission Ambiguity).
  2. **Tối ưu hóa công thức Jacobson/Karels (RFC 6298):**
     - Đảm bảo các phép tính toán sai số RTT được thực hiện bằng số nguyên (Fixed-point arithmetic) để tránh số thực trong kernel.
  3. **Cơ chế Circuit Breaker (Hủy kết nối hỏng):**
     - Đặt ngưỡng tối đa `MAX_BACKOFF = 12`. Nếu số lần truyền lại vượt quá 12 lần liên tiếp, kernel phát cờ hủy kết nối, giải phóng toàn bộ hàng đợi truyền lại và gọi `wakeup(pcb)` để thông báo lỗi `EPIPE` / `ECONNRESET` cho tầng ứng dụng.

---

### TUẦN 4: Chống Nghẽn Hàng Đợi NIC & Tối Ưu Hóa Cấp Phát Mbuf Đa Kết Nối
* **Mục tiêu kỹ thuật:** Đảm bảo tầng mạng chịu tải ổn định khi TV3 chạy Web Server đa luồng (Worker Pool) phục vụ nhiều kết nối đồng thời.
* **Nhiệm vụ cụ thể:**
  1. **Nâng kích thước và quản lý Ring Buffer E1000 (`kernel/e1000.c`):**
     - Tinh chỉnh cơ chế giải phóng Descriptor TX đã truyền xong, dọn dẹp mbuf kịp thời trước khi ring buffer bị đầy.
     - Tránh tình trạng drop gói tại ngắt nhận (RX) khi CPU bận phục vụ các tiến trình HTTP worker.
  2. **Phân tích ngưỡng chịu tải của `mbuf_pool`:**
     - Đánh giá dung lượng RAM hệ thống: Nếu tăng số lượng worker, kích thước pool mbuf hiện tại (128 buffer) có đủ không?
     - Bổ sung thống kê kernel `mbuf_free_count` và xuất cảnh báo khi mbuf xuống dưới ngưỡng 10% (Low Watermark).

---

### TUẦN 5: Xây Dựng Bộ Đo Nhịp Xung Thời Gian Thực Hỗ Trợ Google BBR
* **Mục tiêu kỹ thuật:** Cung cấp hạ tầng đồng hồ thời gian thực độ phân giải microsecond để TV2 hiện thực thuật toán Google BBR.
* **Nhiệm vụ cụ thể:**
  1. **Hạ tầng đo thời gian thực qua RISC-V Hardware Counters:**
     - Xây dựng hàm đọc trực tiếp thanh ghi nhịp thời gian phần cứng của RISC-V: `r_time()` và thanh ghi chu kỳ lệnh `rdcycle()`.
     - Chuyển đổi chu kỳ xung nhịp thành microsecond chính xác, phục vụ tính toán `min_rtt` và ước tính băng thông `delivered_rate` của kết nối.
  2. **Hỗ trợ TV2 tích hợp BBR Engine:**
     - Thiết kế giao diện gọi hàm tính toán độ trễ tối thiểu trong cửa sổ trượt 10 giây (10-second rolling min RTT window).

---

### TUẦN 6: Stress Test Khắc Nghiệt, Chứng Minh Zero-Leak & Đóng Gói
* **Mục tiêu kỹ thuật:** Chứng minh độ bền tuyệt đối của tầng mạng dưới các điều kiện khắc nghiệt nhất.
* **Nhiệm vụ cụ thể:**
  1. **Kiểm thử cực đoan:**
     - Thử nghiệm tỉ lệ mất gói tăng cao lên 20%, 30%, và 50% bằng lệnh `setloss 20`, `setloss 30`.
     - Kiểm chứng hệ thống tự phục hồi mà không bị Kernel Panic hoặc Deadlock.
  2. **Kiểm toán rò rỉ bộ nhớ (Zero Memory Leak Verification):**
     - Viết đoạn mã kiểm tra sau khi tất cả các kết nối đóng hoàn toàn: biến đếm `mbuf_free_count` phải hồi phục chính xác 100% về giá trị ban đầu.
     - Không có bất kỳ mbuf nào bị "bỏ quên" trong hàng đợi truyền lại hoặc bảng ARP.
  3. **Hỗ trợ TV3 quay video demo và hoàn thiện tài liệu nộp:**
     - Trình diễn kịch bản bật/tắt loss rate trực tiếp bằng lệnh `setloss` trên video.

---

## 3. Tiêu Chí Nghiệm Thu (KPIs) Của TV1

| Hạng mục | Chỉ tiêu định lượng | Phương pháp đo lường |
| :--- | :--- | :--- |
| **Deadlock Prevention** | 0 lần treo máy khi tải lặp lại 50 lần | Chạy script bash curl liên tục |
| **Packet Loss Control** | Điều chỉnh 0% - 100% không cần reboot | Chạy lệnh `setloss <rate>` trong xv6 |
| **RTO Accuracy** | 1 tick = 100ms (chuẩn 10 Hz) | Đo thời gian retransmission trên Wireshark |
| **Memory Safety** | 0 memory leak sau 100 kết nối | In thống kê `mbuf_free_count` tại terminal |
| **Transfer Speed** | Tải 1 MB (10% loss) ≤ 5 giây | Lệnh `curl -w "%{time_total}\n"` |
