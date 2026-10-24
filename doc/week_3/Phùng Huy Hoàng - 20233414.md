# TIẾN TRÌNH TUẦN 3 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai Tuần 3: Tinh Chỉnh RTO Chuẩn Quy Tắc Karn & Giới Hạn Retransmission

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 3 (Week 3)
* **Vai trò:** Thành viên 1 (TV1) - Hạ tầng Mạng, Bộ nhớ Mbuf & Driver E1000
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 3

1. **Triển khai Quy tắc Karn (Karn's Algorithm)**:
   - Loại bỏ sự nhập nhằng trong đo RTT (RTT Retransmission Ambiguity): Tuyệt đối không dùng thời gian nhận ACK của các segment đã từng bị truyền lại để cập nhật Smoothed RTT (`srtt`) và RTT Variance (`rttvar`).
2. **Tối ưu hóa công thức Jacobson/Karels (RFC 6298)**:
   - Hiện thực bằng số nguyên cố định (Fixed-point arithmetic) tránh dùng số thực (floating point) trong nhân hệ điều hành.
3. **Cơ chế Circuit Breaker (Hủy kết nối hỏng)**:
   - Giới hạn số lần truyền lại tối đa `MAX_BACKOFF = 12`. Nếu vượt ngưỡng, kernel tự động giải phóng tài nguyên và đánh thức ứng dụng.

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Triển Khai Quy Tắc Karn (`kernel/tcp_timer.c`)
* Thêm trường cờ trong metadata của segment:
  ```c
  #define FLAG_RETRANSMITTED 0x01
  ```
* Trong hàm cập nhật RTT:
  ```c
  void tcp_update_rtt(struct tcp_pcb *pcb, uint32 sample_rtt_ms, int is_retransmit) {
    if (is_retransmit) {
      // QUY TẮC KARN: Bỏ qua mẫu đo RTT nếu segment này từng phải truyền lại
      return;
    }
    // Cập nhật Jacobson/Karels chuẩn RFC 6298:
    if (pcb->srtt == 0) {
      pcb->srtt = sample_rtt_ms;
      pcb->rttvar = sample_rtt_ms / 2;
    } else {
      int delta = (int)sample_rtt_ms - (int)pcb->srtt;
      if (delta < 0) delta = -delta;
      pcb->rttvar = (3 * pcb->rttvar + delta) / 4;
      pcb->srtt = (7 * pcb->srtt + sample_rtt_ms) / 8;
    }
    pcb->rto = pcb->srtt + 4 * pcb->rttvar;
    if (pcb->rto < 200) pcb->rto = 200;   // Min RTO = 200ms
    if (pcb->rto > 2000) pcb->rto = 2000; // Max RTO = 2000ms
  }
  ```

### 2.2. Cơ Chế Circuit Breaker Khi Quá Ngưỡng Retransmit
* Trong `kernel/tcp.c`, nếu đường truyền mạng bị ngắt kết nối hoàn toàn:
  ```c
  if (pcb->backoff > 12) {
    // Đã retransmit 12 lần với Exponential Backoff mà không có phản hồi
    pcb->state = TCP_CLOSED;
    wakeup(pcb);
    if (pcb->sock)
      sock_push_eof((struct socket *)pcb->sock);
    release(&pcb->lock);
    continue;
  }
  ```

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 3

1. **Ổn định giá trị RTO:**
   - Trong quá trình truyền dữ liệu dưới tỉ lệ rớt gói 10%, giá trị RTO dao động ổn định trong khoảng 200ms – 400ms, không bị hiện tượng "phồng RTO" (RTO inflation) lên hàng chục giây như các phiên bản thiếu quy tắc Karn.
2. **Khử kết nối hỏng:**
   - Khi rút dây mạng giả lập: sau 12 lần backoff, hệ thống tự động giải phóng PCB, không còn hiện tượng Zombie Connection chiếm dụng tài nguyên.
