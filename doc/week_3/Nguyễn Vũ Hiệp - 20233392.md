# TIẾN TRÌNH TUẦN 3 - Nguyễn Vũ Hiệp (MSSV: 20233392)

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 3

---

## Công việc đã thực hiện

1. **Quy tắc Karn** (`kernel/net.h`, `mbuf.c`, `tcp_output.c`, `tcp.c`, `tcp_input.c`)
   - Thêm `retransmitted` và `send_tick` vào `struct mbuf`.
   - Đặt `retransmitted = 1` khi truyền lại bằng RTO hoặc Fast Retransmit.
   - `tcp_handle_ack` chỉ gọi `tcp_update_rto` với segment chưa từng gửi lại. Bỏ việc reset cứng `rto = 200`, giữ RTO đã backoff cho tới khi có mẫu RTT hợp lệ.
2. **Jacobson/Karels (RFC 6298)** (`kernel/tcp_timer.c`)
   - Giữ tính toán số nguyên cố định, tách các hệ số `RTT_ALPHA_SHIFT`, `RTT_BETA_SHIFT`, `RTT_K` thành hằng số.
   - `RTO_MIN`/`RTO_MAX` chuyển sang `net.h`.
3. **Circuit Breaker**
   - Thêm `MAX_BACKOFF = 12`. Khi vượt ngưỡng, giải phóng toàn bộ `retransmit_q`, đặt `conn_err = ECONNRESET`, đóng PCB, gọi `wakeup(pcb)` và đẩy EOF cho socket.

## Điều chỉnh sau khi chạy thử (QEMU, `setloss 10`, tải 1 MB bằng curl)

- **Hồi quy tốc độ:** sau ACK mới mà chưa có mẫu RTT, gọi `tcp_recalc_rto` để tính lại RTO từ `srtt`/`rttvar` thay vì giữ giá trị đã backoff (vẫn đúng Karn vì không dùng mẫu từ gói gửi lại).

## Kết quả kiểm thử
- Bộ `tests/run_tests.py`: 6/6 đạt.
- Tải 1 MB ở 10% mất gói: thỉnh thoảng còn treo (0 byte) hoặc thiếu 1.024 byte. Lỗi này có sẵn từ trước (mã gốc cũng bị), do máy trạng thái TCP trong `kernel/tcp_input.c` không xử lý ACK ở các trạng thái `SYN_RCVD`, `FIN_WAIT_1`, `FIN_WAIT_2`, `LAST_ACK` nên `retransmit_q` không được dọn. Lỗi nằm ngoài phạm vi TV1 (thuộc tầng FSM của TV2), ghi nhận để bàn giao.
- Thời gian tải dao động 2,5 đến 13,9 s do thiếu Fast Recovery / NewReno (phạm vi TV2, `tcp_cc.c`).
