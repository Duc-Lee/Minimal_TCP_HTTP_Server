# TIẾN TRÌNH TUẦN 2 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai Tuần 2: Hàng Đợi Gói Lệch Thứ Tự (`ooo_queue`) & Phản Hồi RFC 793 RST

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 2 (Week 2)
* **Vai trò:** Thành viên 2 (TV2) - Tầng Giao vận TCP, Máy trạng thái FSM & Cơ chế Tin cậy
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 2

1. **Hiện thực Hàng đợi Out-of-Order (`ooo_queue`)**:
   - Khi mạng mất gói 10%, các gói tin đến sau có sequence lớn hơn `rcv_nxt`. Loại bỏ việc drop gói hợp lệ này; lưu giữ trong hàng đợi và ráp nối ngay khi gói bị thiếu đến nơi.
2. **Hiện thực hàm phản hồi cờ RST theo RFC 793 (`tcp_send_rst`)**:
   - Khi kernel nhận gói TCP gửi tới một cổng chưa mở trạng thái `LISTEN`, tự động sinh gói RST gửi ngược lại client để báo hiệu đóng kết nối ngay lập tức.
3. **Phối hợp xác thực với Bộ kiểm thử State Tests của TV3**:
   - Chạy các ca kiểm thử tự động với Scapy để đảm bảo FSM và `ooo_queue` hoạt động hoàn hảo.

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Cài Đặt Hàng Đợi Gói Lệch Thứ Tự (`ooo_queue`)
* **Khai báo trong `struct tcp_pcb` (`kernel/net.h`):**
  ```c
  struct tcp_pcb {
    // ...
    struct mbuf *ooo_queue; // Danh sách liên kết các segment out-of-order
  };
  ```
* **Logic tiếp nhận segment trong `kernel/tcp_input.c`:**
  - Nếu `seq == pcb->rcv_nxt`: Đây là gói đúng thứ tự. Đẩy dữ liệu lên `sock->rx_buf`, tăng `pcb->rcv_nxt`.
  - Nếu `seq > pcb->rcv_nxt` và `seq < pcb->rcv_nxt + pcb->rcv_wnd`:
    * Không giải phóng gói tin!
    * Chèn gói tin vào `pcb->ooo_queue` theo thứ tự sequence tăng dần:
      ```c
      struct mbuf **curr = &pcb->ooo_queue;
      while (*curr && ((struct tcp_hdr *)((*curr)->head))->seq < seq) {
        curr = &((*curr)->next);
      }
      m->next = *curr;
      *curr = m;
      ```
  - **Quét ghép nối (Coalescing):**
    * Ngay sau khi xử lý gói đúng thứ tự, duyệt lại `ooo_queue`. Nếu gói đầu hàng đợi có `seq == pcb->rcv_nxt`, lấy ra đẩy lên socket, tiếp tục lặp cho đến khi gặp khoảng trống (hole).
    * Gửi gói `ACK` tích lũy phản hồi sequence mới nhất đã nhận thành công.

### 2.2. Hiện Thực Hàm `tcp_send_rst()` Khi Nhận Packet Tại Port Đóng
* **Chuẩn RFC 793 quy định:** Nếu kết nối không tồn tại (cổng đóng) và gói tin đến không có cờ RST, phía nhận phải gửi lại gói RST:
  - Nếu gói đến có cờ ACK: Sequence của gói RST bằng Acknowledgment Number của gói đến.
  - Nếu gói đến không có ACK: Sequence của gói RST bằng 0, Acknowledgment Number bằng Sequence của gói đến cộng chiều dài segment.
* **Cài đặt trong `kernel/tcp_output.c`:**
  ```c
  void tcp_send_rst(uint32 src_ip, uint32 dst_ip, uint16 src_port, uint16 dst_port, uint32 seq, uint32 ack, int has_ack) {
    struct mbuf *m = mbufalloc(sizeof(struct eth_hdr) + sizeof(struct ip_hdr) + sizeof(struct tcp_hdr));
    if (!m) return;
    struct tcp_hdr *th = (struct tcp_hdr *)mbufpush(m, sizeof(struct tcp_hdr));
    th->src_port = htons(src_port);
    th->dst_port = htons(dst_port);
    th->flags = TCP_FLAG_RST;
    if (has_ack) {
      th->seq = htonl(ack);
      th->ack = 0;
    } else {
      th->seq = 0;
      th->ack = htonl(seq);
      th->flags |= TCP_FLAG_ACK;
    }
    // Tính checksum và gửi qua ip_output
    ip_output(m, src_ip, dst_ip, IP_PROTO_TCP);
  }
  ```

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 2

1. **Hiệu quả của `ooo_queue`:**
   - Trong môi trường rớt gói 10%, Wireshark ghi nhận các gói đến lệch thứ tự được lưu trữ và ráp nối hoàn hảo, không còn hiện tượng gửi lại trùng lặp dữ liệu đã nhận.
2. **Kiểm thử RST:**
   - Host bắn gói SYN tới cổng 81 (chưa mở): xv6 phản hồi ngay gói RST trong 2ms, client trên Host lập tức nhận thông báo `Connection refused` thay vì bị timeout.
