# TIẾN TRÌNH TUẦN 3 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai: Quy Tắc Karn, Chuẩn Hóa RTO Jacobson/Karels (Số Nguyên) & Circuit Breaker Giới Hạn Truyền Lại

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 3 (Week 3)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục tiêu

1. Áp dụng Quy tắc Karn: không lấy mẫu RTT từ segment đã bị truyền lại (tránh nhập nhằng RTT khi mất gói).
2. Chuẩn hóa công thức Jacobson/Karels (RFC 6298) bằng số nguyên cố định, không dùng số thực trong kernel.
3. Đặt ngưỡng `MAX_BACKOFF = 12`: truyền lại quá 12 lần liên tiếp thì hủy kết nối, giải phóng hàng đợi truyền lại và báo lỗi cho tầng trên.

---

## 2. Hiện trạng đầu tuần

Kiểm tra mã nguồn cho thấy:

| Hạng mục | Hiện trạng đầu tuần | Vị trí |
| :--- | :--- | :--- |
| Công thức Jacobson/Karels | Đã dùng số nguyên (srtt ×8, rttvar ×4) nhưng hàm `tcp_update_rto()` **không được gọi ở đâu**, nên RTT chưa từng được đo | `kernel/tcp_timer.c` |
| Quy tắc Karn | Chưa có cờ đánh dấu segment truyền lại | `kernel/net.h` |
| Ngưỡng truyền lại | Có `backoff > 12` nhưng viết cứng, không giải phóng `retransmit_q`, chỉ báo EOF | `kernel/tcp.c` |
| RTO sau mỗi ACK mới | Bị reset cứng về 200 ms | `kernel/tcp_input.c` |

---

## 3. Chi Tiết Triển Khai

### 3.1. Quy tắc Karn (`kernel/net.h`, `mbuf.c`, `tcp_output.c`, `tcp_input.c`, `tcp.c`)
* Thêm vào `struct mbuf` hai trường: `int retransmitted` và `uint32 send_tick`. `mbufalloc()` khởi tạo cả hai bằng 0.
* `mbuf_clone_for_retransmit()` ghi `send_tick = ticks` và `retransmitted = 0` cho bản sao nằm trong `retransmit_q`.
* Đặt `retransmitted = 1` cho segment đầu hàng đợi ở cả hai đường truyền lại: hết RTO (`tcp_timer_process`) và Fast Retransmit (`tcp_handle_ack`).
* `tcp_handle_ack()` duyệt các segment vừa được ACK. Chỉ segment có `retransmitted == 0` mới cho mẫu RTT `(ticks - send_tick) * 100` ms, rồi gọi `tcp_update_rto()`.
* Xv6 chạy timer 10 Hz (1 tick = 100 ms) nên mẫu RTT có độ phân giải thô. Mẫu tối thiểu được đặt là 1 tick (100 ms).

### 3.2. Chuẩn hóa Jacobson/Karels (`kernel/tcp_timer.c`)
* Giữ nguyên cách tính bằng số nguyên cố định, tách các hệ số thành hằng số: `RTT_ALPHA_SHIFT = 3` (α = 1/8), `RTT_BETA_SHIFT = 2` (β = 1/4), `RTT_K = 4`.
* `RTO = srtt + K × rttvar`. Vì `rttvar` lưu nhân 4 nên `K × rttvar / 4 = rttvar` và công thức viết `(srtt >> 3) + rttvar`. `RTT_K` hiện chỉ để ghi chú, chưa tham gia phép tính.
* `RTO_MIN = 200`, `RTO_MAX = 2000` chuyển sang `kernel/net.h` để `tcp.c` dùng chung (thay cho số 2000 viết cứng).
* **Bỏ reset cứng `rto = 200`** trong `tcp_handle_ack()`. Nếu có mẫu RTT mới thì gọi `tcp_update_rto()`.
* Thêm `tcp_recalc_rto()`: khi ACK mới nhưng không có mẫu hợp lệ (toàn segment đã truyền lại), tính lại RTO từ `srtt`/`rttvar` hiện có (hoặc `RTO_MIN` nếu chưa có `srtt`). Nhờ đó RTO không bị kẹt ở giá trị đã nhân đôi do backoff, và vẫn đúng Karn vì không dùng mẫu từ gói gửi lại.

### 3.3. Circuit Breaker (`kernel/net.h`, `kernel/tcp.c`)
* Thêm `#define MAX_BACKOFF 12` và trường `int conn_err` trong `struct tcp_pcb`; `conn_err` được khởi tạo bằng 0 khi cấp PCB.
* Khi `backoff > MAX_BACKOFF` trong `tcp_timer_process()`:
  1. Giải phóng toàn bộ `retransmit_q` bằng `mbuffree()`.
  2. Đặt `rto_deadline = 0` và `conn_err = ECONNRESET` (104).
  3. Chuyển PCB sang `TCP_CLOSED`, gọi `wakeup(pcb)` và `sock_push_eof()`.
* **Giới hạn:** `conn_err` mới được lưu trong PCB. Tầng ứng dụng hiện vẫn chỉ thấy EOF; chưa nối `conn_err` vào `sock_read`/`sock_write`.

---

## 4. Kết Quả Kiểm Thử

Môi trường: QEMU `virt`, E1000 (user-mode network, `hostfwd tcp::20080-:80`), Host dùng `curl`.

### 4.1. Bộ test tự động (`tests/run_tests.py`, loss 0%)

6/6 đạt: `3way_handshake`, `closed_port_rst`, `half_close`, `fragmented_stream`, `largefile_1mb`, `client_abort`. Đã chạy lại sau khi rebase lên `origin/main`.

### 4.2. Tải `largefile.bin` (1 MB) qua `httpd`, `setloss 10`, timeout 20 s

Kết quả của bản commit hiện tại (6 lần liên tiếp, một phiên QEMU):

| Lần | HTTP | Kích thước nhận | Thời gian |
| :---: | :---: | :---: | :---: |
| 1 | 200 | 1,048,576 | 4.74 s |
| 2 | 200 | 1,048,576 | 9.12 s |
| 3 | 000 | 0 | treo đến hết timeout |
| 4 | 200 | 1,045,504 | thiếu 3,072 bytes, treo đến hết timeout |
| 5 | 200 | 1,048,576 | 2.71 s |
| 6 | 200 | 1,030,144 | thiếu 18,432 bytes, treo đến hết timeout |

Bộ sinh số giả ngẫu nhiên dùng seed cố định nên thứ tự các lần chạy tái hiện được giữa các lần khởi động.

### 4.3. So sánh với bản gốc (`HEAD` trước tuần 3, build ở `/tmp`)

Chạy song song hai bản, 5 lần tải mỗi bản, timeout 25 s. Phép đo này dùng **bản đầu tiên** của tuần 3 (chưa có `tcp_recalc_rto`):

| Lần | Bản gốc | Bản tuần 3 (bản đầu) |
| :---: | :---: | :---: |
| 1 | 4.24 s | 7.72 s |
| 2 | 4.81 s | 10.83 s |
| 3 | treo (000, 0 byte) | treo (000, 0 byte) |
| 4 | thiếu 1,024 bytes | thiếu 1,024 bytes |
| 5 | 3.05 s | 2.45 s |

* Lỗi treo và thiếu byte **có sẵn từ trước**, bản gốc cũng bị, nên không do thay đổi của tuần này.
* Bản đầu chậm hơn rõ ở lần 1 và 2. Nguyên nhân nghi ngờ: bỏ reset `rto = 200` làm RTO kẹt ở giá trị đã backoff. Đã thêm `tcp_recalc_rto()` (mục 3.2) để xử lý.
* Sau khi thêm `tcp_recalc_rto()`, lần 1 gần bản gốc (4.74 s so với 4.24 s) nhưng **lần 2 vẫn chậm hơn** (9.12 s so với 4.81 s). Hai phép đo không cùng điều kiện (một phép song song, một phép đơn lẻ) nên chưa kết luận chắc về hồi quy còn lại. Cần chạy lại so sánh trên cùng điều kiện.

### 4.4. Chẩn đoán nguyên nhân lỗi treo và thiếu byte

Thêm log tạm vào `tcp_timer_process()` (đã gỡ) để in trạng thái PCB khi `backoff` chạm 4:

| Trạng thái khi kẹt | `snd_una` / `snd_nxt` (tính từ ISS) | Triệu chứng |
| :--- | :--- | :--- |
| `FIN_WAIT_2` (2 lần) | 1,029,180 / 1,048,637 và 1,043,516 / 1,048,637 | Thiếu byte cuối |
| `ESTABLISHED` (1 lần) | 1 / 22,588 | Treo 0 byte |

Đọc mã [tcp_input.c](../../kernel/tcp_input.c) cho thấy nguyên nhân:
* `FIN_WAIT_1`, `FIN_WAIT_2`, `LAST_ACK` **không gọi `tcp_handle_ack()`**, nên `retransmit_q` không được dọn khi ACK về sau `close()`. Timer chỉ gửi lại gói đầu hàng đợi, các gói mất phía sau không bao giờ đến nơi.
* `FIN_WAIT_1` nhảy sang `FIN_WAIT_2` khi nhận bất kỳ ACK nào, kể cả dup-ACK, dù FIN chưa được ACK.
* `SYN_RCVD` chỉ gán `snd_una = ack` mà không dọn SYN-ACK khỏi `retransmit_q`, nên timer gửi lại SYN-ACK thay vì dữ liệu.

Đã thử sửa bằng cách gọi `tcp_handle_ack()` ở bốn trạng thái trên và chỉ chuyển trạng thái khi `ack >= snd_nxt`. Kết quả thử: 14/14 lần tải 1 MB ở 10% loss đều đủ dữ liệu (2.5 đến 13.9 s), hết treo và hết thiếu byte. **Bản sửa này không nằm trong commit tuần 3** vì thuộc phạm vi máy trạng thái TCP của TV2, chỉ ghi nhận để bàn giao.

### 4.5. Phân tích thời gian dao động

Trên 6 lần tải (có bản sửa FSM tạm ở mục 4.4): **96** lần hết RTO ở lần backoff đầu, 13 lần ở backoff 2, 1 lần ở backoff 3, và **597** lần Fast Retransmit. Phần lớn RTO giữ ở 200 ms (`RTO_MIN`), cho thấy RTO hội tụ ổn định và không bị trôi. Thời gian tải dao động vì mỗi cửa sổ mất nhiều gói chỉ sửa được một gói mỗi lượt (chưa có Fast Recovery / NewReno).

---

## 5. Tồn Đọng & Việc Tiếp Theo

* **KPI "tải 1 MB với 10% loss ≤ 5 giây" chưa đạt ổn định.** Còn lỗi treo và thiếu byte (mục 4.2), và thời gian dao động 2.5 đến 13.9 s.
* **Lỗi FSM ở `FIN_WAIT_1`/`FIN_WAIT_2`/`LAST_ACK`/`SYN_RCVD`** (mục 4.4): cần TV2 tích hợp bản sửa. Nên bổ sung bài test có `setloss` vào `tests/`, vì bộ test hiện tại chạy ở loss 0% nên không bắt được lỗi này.
* **Fast Recovery / NewReno** thuộc `tcp_cc.c` của TV2: cần cho tốc độ ổn định. Sau khi TV2 hoàn tất, đo lại KPI.
* Nối `conn_err` vào `sock_read`/`sock_write` để trả `ECONNRESET`/`EPIPE` thật cho ứng dụng (phối hợp TV3, `kernel/sock.c`).
* Chạy lại so sánh với bản gốc trên cùng điều kiện (mục 4.3) để kết luận về hồi quy tốc độ còn lại ở lần tải 2.
* Chưa kiểm chứng riêng nhánh Circuit Breaker (`backoff > 12`) bằng kịch bản thật, chưa từng xảy ra trong các lần đo; mới kiểm tra bằng đọc mã và biên dịch.

---

## 6. Danh Sách Tệp Thay Đổi

| Tệp | Thay đổi |
| :--- | :--- |
| `kernel/net.h` | Thêm `retransmitted`, `send_tick` vào `struct mbuf`; `conn_err` vào `struct tcp_pcb`; `RTO_MIN`, `RTO_MAX`, `MAX_BACKOFF`, `ECONNRESET`; khai báo `tcp_recalc_rto()` |
| `kernel/mbuf.c` | Khởi tạo `retransmitted`, `send_tick` trong `mbufalloc()` |
| `kernel/tcp_output.c` | Ghi `send_tick`, `retransmitted = 0` cho bản sao trong `retransmit_q` |
| `kernel/tcp_input.c` | Lấy mẫu RTT theo Karn trong `tcp_handle_ack()`, đặt `retransmitted` khi Fast Retransmit, bỏ reset cứng `rto = 200` |
| `kernel/tcp_timer.c` | Hằng số `RTT_ALPHA_SHIFT`/`RTT_BETA_SHIFT`/`RTT_K`, thêm `tcp_recalc_rto()` |
| `kernel/tcp.c` | Đặt `retransmitted` khi hết RTO, dùng `RTO_MAX`/`MAX_BACKOFF`, Circuit Breaker giải phóng `retransmit_q` và đặt `conn_err` |
