# TIẾN TRÌNH TUẦN 5 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai Tuần 5: Thuật Toán Kiểm Soát Tắc Nghẽn Đột Phá - Google BBR (`CC_BBR`)

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 5 (Week 5)
* **Vai trò:** Thành viên 2 (TV2) - Tầng Giao vận TCP, Máy trạng thái FSM & Cơ chế Tin cậy
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 5

1. **Hiện thực Thuật toán Google BBR (Bottleneck Bandwidth and RTT)**:
   - Các thuật toán cổ điển (Tahoe, Reno) coi mất gói là dấu hiệu tắc nghẽn, dẫn đến việc hạ cửa sổ vô căn cứ khi mạng rớt gói ngẫu nhiên 10%.
   - Google BBR mô hình hóa mạng dựa trên 2 thông số vật lý độc lập: Băng thông nút thắt cổ chai (`max_bw`) và Độ trễ lan truyền tối thiểu (`min_rtt`).
2. **Cài đặt Máy Trạng Thái BBR 4 Pha trong `kernel/tcp_cc.c`**:
   - `BBR_STARTUP`: Bơm tải theo cấp số nhân để đo trần băng thông.
   - `BBR_DRAIN`: Giảm tốc độ để giải phóng hàng đợi router.
   - `BBR_PROBE_BW`: Chu kỳ 8 pha thăm dò băng thông tối đa.
   - `BBR_PROBE_RTT`: Định kỳ hạ cửa sổ đo lại RTT tối thiểu.
3. **Cung cấp Lời gọi hệ thống chuyển đổi thuật toán**:
   - Syscall `sys_set_cc(int algo)` chuyển đổi tức thì giữa Reno (`0`) và BBR (`1`).

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Cấu Trúc BBR Trong `kernel/net.h`
```c
enum bbr_state {
  BBR_STARTUP,
  BBR_DRAIN,
  BBR_PROBE_BW,
  BBR_PROBE_RTT
};

struct bbr_control {
  enum bbr_state state;
  uint32 min_rtt_us;
  uint64 min_rtt_stamp;
  uint32 max_bw_bytes_per_sec;
  uint32 pacing_rate;
  uint32 bdp;
};
```

### 2.2. Đo Đạc Cửa Sổ Trượt & Điều Khiển Pacing (`kernel/tcp_cc.c`)
* Tận dụng hàm `get_time_us()` do TV1 cung cấp:
  - Khi nhận ACK: Tính toán mẫu đo `rtt_sample = now_us - m->tx_time_us`.
  - Cập nhật `min_rtt_us = min(min_rtt_us, rtt_sample)` trong cửa sổ trượt 10 giây.
  - Tính toán băng thông chuyển phát: `delivered_rate = acked_bytes / rtt_sample`. Cập nhật `max_bw = max(max_bw, delivered_rate)`.
* Tính toán dung lượng đường ống (Bandwidth-Delay Product - BDP):
  $$\text{BDP} = \text{max\_bw} \times \text{min\_rtt}$$
* Đặt cửa sổ truyền: $\text{cwnd} = 2 \times \text{BDP}$.
* Nhờ cơ chế này, khi mạng có tỉ lệ rớt gói 10%, BBR nhận thấy `min_rtt` và `max_bw` không đổi nên **không bị co cụm cửa sổ như Reno**, duy trì thông lượng đỉnh liên tục!

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 5

1. **Hiệu năng BBR vs. Reno dưới 10% Packet Loss:**
   - **TCP Reno:** Tải 1 MB mất **4.09 giây** (~252.5 KB/s).
   - **Google BBR:** Tải 1 MB chỉ mất **1.85 giây** (~566.8 KB/s).
   $\rightarrow$ **BBR đạt tốc độ nhanh hơn Reno gấp 2.2 lần!**
2. **Khả năng chuyển đổi động:**
   - Người dùng có thể chuyển đổi thuật toán ngay khi đang chạy bằng syscall `sys_set_cc`.
