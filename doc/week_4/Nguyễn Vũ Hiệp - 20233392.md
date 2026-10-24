# TIẾN TRÌNH TUẦN 4 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai Tuần 4: TCP Window Scale (RFC 7323) & Mở Rộng Hàng Đợi Backlog

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 4 (Week 4)
* **Vai trò:** Thành viên 2 (TV2) - Tầng Giao vận TCP, Máy trạng thái FSM & Cơ chế Tin cậy
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 4

1. **Hỗ trợ Tùy chọn TCP Window Scale Option (RFC 7323)**:
   - Các trình duyệt web hiện đại (Chrome, Firefox) luôn gửi tùy chọn Window Scale trong gói SYN để mở rộng cửa sổ truyền nhận vượt trần 64 KB.
   - Phân tích và thỏa hiệp hệ số dịch bit (Scale Factor) trong quá trình bắt tay 3 bước.
2. **Nâng cấp Cơ chế Hàng Đợi Kết Nối Lắng Nghe (Listen Backlog)**:
   - Tách biệt rõ ràng giữa **SYN Queue** (kết nối đang bắt tay) và **Accept Queue** (kết nối đã sẵn sàng cho `accept()`).
   - Đảm bảo khi một worker đang bận tải file nặng, các client khác vẫn hoàn tất bắt tay 3 bước thành công mà không bị từ chối kết nối.

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Phân Tích TCP Options & Window Scale (`kernel/tcp_input.c`)
* Cấu trúc tùy chọn TCP Option Window Scale (Kind = 3, Length = 3, Shift count):
  ```c
  #define TCP_OPT_WSCALE 3
  ```
* Trong hàm xử lý gói SYN đến:
  - Duyệt trường options của TCP Header (bắt đầu sau 20 byte tiêu đề chuẩn):
    ```c
    uint8 *opt = (uint8 *)(th + 1);
    int opt_len = ((th->off >> 4) * 4) - sizeof(struct tcp_hdr);
    while (opt_len > 0) {
      if (*opt == 0) break; // EOL
      if (*opt == 1) { opt++; opt_len--; continue; } // NOP
      if (*opt == TCP_OPT_WSCALE && opt_len >= 3) {
        pcb->snd_wscale = *(opt + 2);
        if (pcb->snd_wscale > 14) pcb->snd_wscale = 14;
        pcb->rcv_wscale = 2; // xv6 scale factor = 2 (nhân 4)
        break;
      }
      int len = *(opt + 1);
      if (len <= 0) break;
      opt += len;
      opt_len -= len;
    }
    ```
  - Tính toán kích thước cửa sổ thực: `window_size = (uint32)th->win << pcb->snd_wscale`.

### 2.2. Hoàn Thiện Hàng Đợi Backlog Đa Kết Nối
* Trong `struct socket`, duy trì con trỏ danh sách liên kết các PCB đã hoàn tất bắt tay:
  ```c
  struct socket {
    // ...
    struct tcp_pcb *backlog_q;
    int backlog_count;
    int backlog_max;
  };
  ```
* Khi nhận ACK cuối cùng của bắt tay 3 bước:
  - Nếu `backlog_count < backlog_max`: Đưa PCB vào `backlog_q` và gọi `wakeup(&sock->backlog_q)`.
  - Các worker đang chờ trong `sys_accept()` sẽ lần lượt được đánh thức để lấy kết nối ra phục vụ độc lập.

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 4

1. **Khả năng tương thích Window Scale:**
   - Wireshark bắt gói tin xác nhận phiên bắt tay với trình duyệt Chrome thương thảo thành công Window Scale, dung lượng cửa sổ trượt được mở rộng tự động.
2. **Phục vụ đồng thời:**
   - 8 client kết nối cùng lúc không gặp lỗi `Connection refused` nhờ hàng đợi backlog hoạt động ổn định.
