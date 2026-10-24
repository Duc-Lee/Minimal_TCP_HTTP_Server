# TIẾN TRÌNH TUẦN 1 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai Tuần 1: Đồng Bộ Hóa TCP FSM, Rà Soát Khóa PCB & Xác Thực Checksum

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 1 (Week 1)
* **Vai trò:** Thành viên 2 (TV2) - Tầng Giao vận TCP, Máy trạng thái FSM & Cơ chế Tin cậy
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 1

Trong tuần đầu tiên của dự án, Thành viên 2 tập trung rà soát và gia cố toàn bộ lõi tầng giao vận TCP (Layer 4) để đảm bảo hệ thống vận hành an toàn trên môi trường đa nhân SMP (Symmetric Multiprocessing) của xv6:
1. **Kiểm toán an toàn luồng (Thread-Safety)** đối với cấu trúc `struct tcp_pcb`: Rà soát thứ tự khóa `pcb->lock` tại mọi điểm chuyển trạng thái FSM.
2. **Khắc phục Race Condition giữa Ngắt mạng & Timer**: Đảm bảo không xảy ra xung đột khi luồng ngắt `e1000_intr()` tiếp nhận gói ACK cùng lúc với tiến trình định thời `tcp_timer_process()` đang quét hàng đợi retransmission.
3. **Xác thực giải thuật TCP Checksum (RFC 1071)**: Đảm bảo tính toán chính xác trên cả dữ liệu chẵn byte và lẻ byte kết hợp với TCP Pseudo-Header.
4. **Phối hợp chuẩn hóa RTO Timer & Xử lý cờ RST**: Hỗ trợ TV1 hiệu chỉnh công thức nhịp xung 10 Hz và tích hợp nhánh phục hồi cờ `TCP_FLAG_RST` theo RFC 793.
5. **Khảo sát cấu trúc hàng đợi Out-of-Order (`ooo_queue`)**: Thiết kế sơ bộ danh sách liên kết lưu trữ gói tin đến lệch thứ tự chuẩn bị cho Tuần 2.

---

## 2. Chi Tiết Các Công Việc Đã Thực Hiện

### 2.1. Rà Soát Khóa `pcb->lock` & Chống Race Condition Trên Đa Nhân
* **Vấn đề phát hiện:**
  - Trên xv6-riscv cấu hình đa nhân (`CPUS = 3`), ngắt mạng từ card E1000 có thể được phục vụ trên CPU 1, trong khi `tcp_timer_process()` hoặc tiến trình `httpd` chạy trên CPU 0 hoặc CPU 2.
  - Ban đầu, hàm duyệt hàng đợi truyền lại `tcp_timer_process()` có một số đoạn truy cập trực tiếp vào `pcb->retransmit_q` mà không nắm giữ khóa `pcb->lock`, dẫn đến hiện tượng con trỏ danh sách liên kết bị lỗi (dangling pointer) khi luồng ngắt nhận ACK vừa giải phóng segment tương ứng.
* **Giải pháp triển khai:**
  - Đảm bảo quy tắc bất biến: Bất kỳ thao tác đọc/ghi nào trên trường FSM `pcb->state`, danh sách `pcb->retransmit_q`, hoặc cập nhật `pcb->snd_una`, `pcb->snd_nxt` đều phải được bao bọc chặt chẽ bởi:
    ```c
    acquire(&pcb->lock);
    // Thao tác an toàn trên PCB
    release(&pcb->lock);
    ```
  - Loại bỏ hoàn toàn tình trạng Deadlock giữa khóa socket (`sock->lock`) và khóa PCB (`pcb->lock`) bằng cách quy định thứ tự khóa nhất quán: Luôn lấy `pcb->lock` trước, sau đó mới lấy `sock->rx_buf.lock` hoặc `sock->tx_buf.lock`.

### 2.2. Kiểm Tra Tính Đúng Đắn Của RFC 1071 TCP Checksum
* **Vấn đề phân tích:**
  - TCP Checksum bao gồm 12 byte TCP Pseudo-Header (Source IP, Dest IP, Zero, Protocol = 6, TCP Length) cộng với TCP Header và Data Payload.
  - Khi payload có độ dài là số lẻ byte (ví dụ HTTP response header kết thúc lẻ), byte cuối cùng cần được dịch bit chính xác vào nửa trên của word 16-bit theo thứ tự Big-Endian của mạng:
    ```c
    if (len == 1) {
      sum += (uint32)(*(uint8 *)data) << 8;
    }
    ```
* **Kết quả:** Kiểm tra với công cụ Wireshark trên Host xác nhận toàn bộ các segment phát ra từ xv6 đều có trường `Checksum: 0x.... [correct]` và không có gói tin nào bị drop do Checksum Mismatch.

### 2.3. Hỗ Trợ Tích Hợp Xử Lý Cờ RST (RFC 793) & Hiệu Chuẩn RTO
* Phối hợp cùng TV1 (Phùng Huy Hoàng) phân tích nguyên nhân Web Server bị treo khi lệnh curl chạm mốc 30s timeout:
  - Khi client ngắt kết nối, cờ RST được gửi sang.
  - Triển khai logic đóng kết nối và đánh thức socket trong `kernel/tcp_input.c`:
    ```c
    if (flags & TCP_FLAG_RST) {
      if (pcb->state != TCP_LISTEN && pcb->state != TCP_CLOSED) {
        pcb->state = TCP_CLOSED;
        wakeup(pcb);
        if (pcb->sock)
          sock_push_eof((struct socket *)pcb->sock);
      }
      release(&pcb->lock);
      mbuffree(m);
      return;
    }
    ```

### 2.4. Thiết Kế Sơ Bộ Cho Hàng Đợi Gói Lệch Thứ Tự (`ooo_queue`)
* Chuẩn bị cho Tuần 2: Phác thảo cấu trúc đưa vào `struct tcp_pcb` trong `kernel/net.h`:
  ```c
  struct mbuf *ooo_queue; // Hang doi cac segment den lech thu tu (seq > rcv_nxt)
  ```
* Thiết kế giải thuật chèn gói tin vào danh sách liên kết theo thứ tự tăng dần của sequence number và thuật toán quét nối dữ liệu khi gói tin bị thiếu đến nơi.

---

## 3. Kết Quả Nghiệm Thu Tuần 1

1. **Độ ổn định đa nhân (SMP Stability):**
   - Chạy kiểm thử QEMU với tùy chọn `-smp 3`, chạy 20 lần tải liên tiếp: không xảy ra bất kỳ lỗi Race Condition, Kernel Panic hay Kernel Trap nào.
2. **Kiểm tra Checksum:**
   - 100% các gói tin IP và TCP được bên nhận chấp thuận hợp lệ.
3. **Phục hồi sau Client RST:**
   - Ngắt curl bằng `Ctrl+C` giữa chừng: Server ngay lập tức thu hồi kết nối và sẵn sàng nhận kết nối mới sau 0.01s.
