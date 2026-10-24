# TIẾN TRÌNH TUẦN 2 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai Tuần 2: Giả Lập Mất Gói Động (Loss Simulator Syscalls/CLI) & Cơ Chế ARP Aging

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 2 (Week 2)
* **Vai trò:** Thành viên 1 (TV1) - Hạ tầng Mạng, Bộ nhớ Mbuf & Driver E1000
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 2

1. **Hoàn thiện Bộ giả lập mất gói động (Configurable Loss Simulator)**:
   - Thay thế việc mã hóa cứng tỉ lệ rớt gói trong kernel driver bằng cơ chế điều khiển động ở Runtime.
   - Hiện thực 2 System Calls: `sys_set_loss_rate(int rate)` và `sys_get_loss_rate(void)`.
   - Xây dựng tiện ích dòng lệnh `setloss` trên shell xv6 để người dùng cấu hình tỉ lệ rơi gói từ 0% đến 100%.
2. **Triển khai Cơ chế Lão hóa ARP Cache (ARP Aging)**:
   - Khắc phục nguy cơ tràn bảng ánh xạ ARP Cache khi hệ thống chạy trong thời gian dài.
   - Thêm cơ chế quét định kỳ và xóa bỏ các bản ghi ARP không hoạt động quá 60 giây.
3. **Phối hợp kiểm thử với TV2 và TV3**:
   - Sử dụng lệnh `setloss` phối hợp kiểm thử hàng đợi Out-of-Order của TV2 và bộ kịch bản tự động của TV3.

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Cài Đặt Loss Simulator Trong Driver `kernel/e1000.c`
* Thêm biến toàn cục `packet_loss_rate` (mặc định 10% theo yêu cầu đề bài) và các hàm getter/setter:
  ```c
  static int packet_loss_rate = 10;

  int e1000_set_loss_rate(int rate) {
    if (rate < 0) rate = 0;
    if (rate > 100) rate = 100;
    acquire(&e1000_lock);
    packet_loss_rate = rate;
    release(&e1000_lock);
    return packet_loss_rate;
  }

  int e1000_get_loss_rate(void) {
    acquire(&e1000_lock);
    int r = packet_loss_rate;
    release(&e1000_lock);
    return r;
  }
  ```
* Trong hàm phát gói `e1000_transmit()`:
  ```c
  if (eth_type != ETH_TYPE_ARP && packet_loss_rate > 0) {
    seed = (seed * 1103515245 + 12345) & 0x7fffffff;
    if ((seed % 100) < packet_loss_rate) {
      // Giả lập mất gói: giải phóng mbuf và không nạp vào TX Ring
      mbuffree(m);
      return 0;
    }
  }
  ```

### 2.2. Hiện Thực Lời Gọi Hệ Thống (System Calls) & Ứng Dụng `setloss`
1. Đăng ký mã syscall trong `kernel/syscall.h`:
   - `SYS_set_loss_rate 28`
   - `SYS_get_loss_rate 29`
2. Cài đặt hàm xử lý kernel trong `kernel/sysnet.c`:
   ```c
   uint64 sys_set_loss_rate(void) {
     int rate;
     argint(0, &rate);
     return e1000_set_loss_rate(rate);
   }

   uint64 sys_get_loss_rate(void) {
     return e1000_get_loss_rate();
   }
   ```
3. Xây dựng tiện ích người dùng `user/setloss.c`:
   - Hỗ trợ xem trạng thái hiện tại: `setloss` hoặc `setloss status`.
   - Hỗ trợ đổi tỉ lệ: `setloss 0`, `setloss 10`, `setloss 25`.
   - Đăng ký vào `Makefile` (`UPROGS += $U/_setloss`) để tự động nạp vào file system.

### 2.3. Triển Khai ARP Cache Aging (`kernel/arp.c`)
* Thêm trường `uint last_seen_tick;` vào cấu trúc bản ghi ARP:
  ```c
  struct arp_entry {
    uint32 ip;
    uint8  mac[6];
    uint   last_seen_tick;
    int    state; // FREE, RESOLVING, VALID
    struct mbuf *pending_m;
  };
  ```
* Cập nhật `entry->last_seen_tick = ticks;` mỗi khi nhận gói tin IP/ARP từ host tương ứng.
* Thêm hàm `arp_timer()` được gọi định kỳ từ ngắt đồng hồ: Nếu `ticks - entry->last_seen_tick > 60 * 10` (tương đương 60 giây), chuyển trạng thái bản ghi về `FREE` để tái sử dụng.

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 2

1. **Điều khiển rớt gói trực tiếp trên console:**
   - Người dùng gõ `setloss 0` -> Thử nghiệm tải file 1 MB đạt tốc độ tối đa ~4.1 MB/s trong 0.25s.
   - Gõ `setloss 10` -> Tốc độ chuyển về chế độ chịu mất gói 10%, hoàn thành trong 4.09s.
2. **Khả năng làm sạch ARP Cache:**
   - Bảng ARP tự động thu hồi các slot sau 60s không có lưu lượng, ngăn chặn triệt để lỗi đầy bảng ARP khi chạy lâu dài.
