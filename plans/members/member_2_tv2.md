# KẾ HOẠCH HÀNH ĐỘNG CHI TIẾT - THÀNH VIÊN 2 (TV2)
## Chuyên môn: Tầng Giao Vận TCP, Máy Trạng Thái FSM & Kiểm Soát Tắc Nghẽn (Reno & BBR)

---

## 1. Thông Tin Thành Viên & Trách Nhiệm

* **Vai trò:** Kỹ sư Tầng Giao vận TCP & Kiểm soát Tắc nghẽn (*TCP Transport Layer, State Machine & Congestion Control Engineer*).
* **Tỷ trọng đóng góp:** 33.3% khối lượng toàn bộ dự án.
* **Danh mục mã nguồn phụ trách chính:**
  * `kernel/tcp.c`: Quản lý danh sách bảng PCB (Protocol Control Block), cấp phát, tìm kiếm, khởi tạo và hủy kết nối.
  * `kernel/tcp_input.c`: Tiếp nhận phân tích gói tin TCP, xử lý bắt tay 3 bước, cờ ACK, FIN, RST, kiểm tra thứ tự Sequence.
  * `kernel/tcp_output.c`: Đóng gói segment TCP, tính toán tiêu đề TCP, gửi dữ liệu tuân thủ cửa sổ trượt và cửa sổ tắc nghẽn.
  * `kernel/tcp_cc.c`: Thuật toán kiểm soát tắc nghẽn: TCP Tahoe, TCP Reno (RFC 5681) và Google BBR.
  * `tests/state_tests.py`: Bộ kịch bản kiểm thử tự động máy trạng thái TCP FSM sử dụng Python và Scapy.

---

## 2. Lộ Trình 6 Tuần Chi Tiết Của Thành Viên 2

### TUẦN 1: Chuẩn Hóa Khóa PCB Locking & Xử Lý Cờ RST Khắc Phục Treo Server
> **Trạng thái:** ✅ Đã hoàn thành cốt lõi (Đã tích hợp cùng TV1 tại `f30886a`).

* **Mục tiêu kỹ thuật:** Đảm bảo an toàn luồng (thread-safety) khi truy cập cấu trúc TCP PCB giữa luồng ngắt mạng và luồng timer, khắc phục việc server bị tê liệt khi client hủy kết nối.
* **Nhiệm vụ cụ thể:**
  1. **Đồng bộ hóa khóa `pcb->lock`:**
     - Rà soát toàn bộ các điểm truy cập PCB trong `kernel/tcp.c`, `kernel/tcp_input.c` và `kernel/tcp_timer.c`. Đảm bảo luôn giữ khóa trước khi thay đổi trạng thái FSM để ngăn chặn Race Condition.
  2. **Bỏ hack ép kiểu dữ liệu trong `tcp_output.c` & `tcp_input.c`:**
     - Xóa bỏ việc ép kiểu `*(uint32 *)(clone->buf) = seq_end`, đồng bộ với cấu trúc `clone->seq_end` mới từ TV1.
  3. **Xử lý cờ `TCP_FLAG_RST` theo RFC 793:**
     - Trong `tcp_input()`, bổ sung khối xử lý khi nhận gói tin có cờ RST từ phía đối tác (ví dụ client dùng `curl` bị timeout hoặc ngắt `Ctrl+C`).
     - Lập tức chuyển trạng thái `pcb->state = TCP_CLOSED`, giải phóng hàng đợi gửi lại `retransmit_q`, gọi `sock_push_eof()` và `wakeup(pcb)` để đánh thức luồng Web Server đang chờ, tránh làm Web Server bị treo ở các kết nối sau.

---

### TUẦN 2: Hàng Đợi Gói Lệch Thứ Tự (`ooo_queue`) & Bộ Test Tự Động Scapy
* **Mục tiêu kỹ thuật:** Tối ưu hóa truyền tải qua mạng mất gói bằng cách giữ lại các gói đến lệch thứ tự thay vì vứt bỏ; xây dựng bộ test tự động kiểm thử 11 trạng thái FSM.
* **Nhiệm vụ cụ thể:**
  1. **Triển khai Hàng đợi Out-of-Order (`ooo_queue`):**
     - Thêm con trỏ `struct mbuf *ooo_queue;` vào cấu trúc `struct tcp_pcb` trong `kernel/net.h`.
     - Trong `kernel/tcp_input.c`: Khi nhận được gói tin có $\text{seq} > \text{rcv\_nxt}$ (trong phạm vi Receive Window), **không được giải phóng gói**, mà chèn vào danh sách liên kết `ooo_queue` sắp xếp tăng dần theo sequence number.
     - Khi gói tin bị thiếu đến nơi ($\text{seq} == \text{rcv\_nxt}$): Chuyển dữ liệu của gói này lên socket, sau đó duyệt ngay hàng đợi `ooo_queue` để lấy toàn bộ các đoạn dữ liệu liên tục kế tiếp chuyển lên socket một thể, rồi gửi ACK tích lũy tương ứng.
  2. **Phản hồi cờ RST khi nhận gói tại cổng đóng:**
     - Cài đặt hàm `tcp_send_rst()`: Khi kernel nhận được gói tin gửi tới một cổng TCP mà không có socket nào đang ở trạng thái `LISTEN`, tự động sinh gói tin có cờ `RST` phản hồi lại client theo đúng chuẩn RFC 793.
  3. **Xây dựng bộ kiểm thử tự động `tests/state_tests.py`:**
     - Sử dụng Python và thư viện Scapy trên máy Host để gửi các gói tin đặc thù kiểm thử:
       * Test bắt tay 3 bước chuẩn (SYN -> SYN-ACK -> ACK).
       * Test gửi SYN vào cổng đóng (Port 81) -> Xác minh nhận lại gói RST từ xv6.
       * Test đóng kết nối nửa vời (Half-close): Client gửi FIN -> Xác minh xv6 vào `CLOSE_WAIT`.
       * Test giả lập drop SYN-ACK và kiểm tra khả năng phục hồi bắt tay lại.

---

### TUẦN 3: Động Cơ Kiểm Soát Tắc Nghẽn TCP Tahoe & TCP Reno (RFC 5681)
* **Mục tiêu kỹ thuật:** Triển khai đầy đủ thuật toán kiểm soát tắc nghẽn công nghiệp, tự động điều tiết tốc độ theo trạng thái mạng.
* **Nhiệm vụ cụ thể:**
  1. **Mở rộng `struct tcp_pcb`:**
     - Bổ sung các biến quản lý tắc nghẽn: `uint32 cwnd` (Congestion Window), `uint32 ssthresh` (Slow Start Threshold), `uint32 dupack_cnt`, `uint32 recover`.
  2. **Viết logic thuật toán trong `kernel/tcp_cc.c`:**
     - **Slow Start:** Khi $\text{cwnd} < \text{ssthresh}$, mỗi khi nhận được 1 ACK hợp lệ:
       $$\text{cwnd} \leftarrow \text{cwnd} + \text{MSS}$$
     - **Congestion Avoidance:** Khi $\text{cwnd} \ge \text{ssthresh}$, mỗi khi nhận được 1 ACK mới:
       $$\text{cwnd} \leftarrow \text{cwnd} + \frac{\text{MSS} \times \text{MSS}}{\text{cwnd}}$$
     - **Fast Retransmit & Fast Recovery (RFC 5681):**
       * Khi nhận đúng 3 Duplicate ACKs liên tiếp:
         $$\text{ssthresh} \leftarrow \max\left(\frac{\text{FlightSize}}{2}, 2 \times \text{MSS}\right)$$
         $$\text{cwnd} \leftarrow \text{ssthresh} + 3 \times \text{MSS}$$
       * Gọi hàm truyền lại ngay lập tức (không cần chờ hết hạn RTO timer).
       * Tăng `cwnd` thêm 1 MSS cho mỗi Duplicate ACK tiếp theo. Khi nhận ACK mới, đặt $\text{cwnd} \leftarrow \text{ssthresh}$ và chuyển về Congestion Avoidance.
  3. **Liên kết với `kernel/tcp_output.c`:**
     - Điều kiện gửi dữ liệu phải tuân thủ cả cửa sổ nhận lẫn cửa sổ tắc nghẽn:
       $$\text{snd\_nxt} - \text{snd\_una} < \min(\text{snd\_wnd}, \text{cwnd})$$

---

### TUẦN 4: Hỗ Trợ Tùy Chọn TCP (Window Scale) & Mở Rộng Hàng Đợi Kết Nối
* **Mục tiêu kỹ thuật:** Tối ưu hóa thông lượng và hỗ trợ kết nối đồng thời từ nhiều client.
* **Nhiệm vụ cụ thể:**
  1. **TCP Window Scale Option (RFC 7323):**
     - Phân tích trường TCP Options trong gói tin SYN từ client (ví dụ Chrome/Firefox thường gửi Window Scale).
     - Cho phép mở rộng Receive Window vượt qua giới hạn cổ điển 64 KB, tăng thông lượng truyền tải trên các liên kết có băng thông lớn.
  2. **Quản lý hàng đợi kết nối (Listen Backlog Queue):**
     - Phối hợp với TV3 hoàn thiện cơ chế phân tách giữa **SYN Queue** (kết nối đang bắt tay dở dang) và **Accept Queue** (kết nối đã hoàn tất bắt tay, đang chờ Web Server gọi `accept()`).
     - Đảm bảo khi một worker đang bận đọc file, các client khác vẫn có thể hoàn tất bắt tay 3 bước mà không bị từ chối kết nối.

---

### TUẦN 5: Hiện Thực Thuật Toán Kiểm Soát Tắc Nghẽn Đột Phá: Google BBR (`CC_BBR`)
* **Mục tiêu kỹ thuật:** Triển khai thuật toán tiên tiến nhất thế giới hiện nay của Google trên một hệ điều hành giáo dục, đạt điểm thưởng xuất sắc.
* **Nhiệm vụ cụ thể:**
  1. **Nguyên lý Google BBR (Bottleneck Bandwidth and Round-trip propagation time):**
     - BBR không coi mất gói là dấu hiệu của tắc nghẽn (đặc biệt quan trọng với môi trường mạng vô tuyến hoặc rớt gói ngẫu nhiên 10%).
     - Tận dụng hạ tầng đọc nhịp phần cứng từ TV1 để đo:
       * `min_rtt`: Độ trễ nhỏ nhất ghi nhận trong 10 giây gần nhất.
       * `max_bw`: Băng thông chuyển phát tối đa (`delivered_bytes / elapsed_time`).
  2. **Máy trạng thái BBR trong `kernel/tcp_cc.c`:**
     - Thiết kế 4 trạng thái hoạt động:
       * `BBR_STARTUP`: Tăng tốc độ bơm dữ liệu theo cấp số nhân để nhanh chóng lấp đầy băng thông.
       * `BBR_DRAIN`: Giảm tốc độ để giải phóng hàng đợi đệm dư thừa tại router.
       * `BBR_PROBE_BW`: Chu kỳ luân phiên thăm dò băng thông (chu kỳ 8 pha).
       * `BBR_PROBE_RTT`: Định kỳ hạ cửa sổ truyền về 4 gói để đo đạc lại `min_rtt` thực tế.
  3. **Hệ thống System Call chuyển đổi thuật toán:**
     - Thêm syscall `sys_set_cc(int algo)` cho phép chuyển đổi tức thì giữa Reno (`0`) và BBR (`1`) để thực hiện các bài benchmark so sánh trực tiếp.

---

### TUẦN 6: Kiểm Thử Tình Huống Biên, Thu Hồi Tài Nguyên & Đo Lường So Sánh
* **Mục tiêu kỹ thuật:** Đảm bảo toàn bộ các trạng thái kết nối được thu hồi sạch sẽ, hoàn thiện số liệu khoa học chứng minh hiệu quả của thuật toán Reno và BBR.
* **Nhiệm vụ cụ thể:**
  1. **Kiểm tra tình huống biên (Corner Cases):**
     - Client bị ngắt đột ngột (rút dây mạng hoặc ngắt tiến trình `curl` giữa chừng): Kiểm tra PCB có được đưa vào `CLOSED` và thu hồi sạch sẽ hay không.
     - Kiểm tra trạng thái `TIME_WAIT`: Đảm bảo PCB được lưu giữ đúng 2 MSL trước khi tái sử dụng socket, tránh nhiễu gói tin của phiên làm việc cũ.
  2. **Thực hiện Benchmark so sánh khoa học:**
     - Lập bảng so sánh tốc độ tải file 1 MB khi mất gói 10% giữa:
       * TCP cơ bản (Không tắc nghẽn - Stop and Wait / Fixed Window).
       * TCP Reno (Mức 2).
       * Google BBR (Mức 3).
     - Xuất dữ liệu log vẽ đồ thị biến thiên cửa sổ `cwnd` theo thời gian.

---

## 3. Tiêu Chí Nghiệm Thu (KPIs) Của TV2

| Hạng mục | Chỉ tiêu định lượng | Phương pháp đo lường |
| :--- | :--- | :--- |
| **Out-of-Order Handling** | Giữ lại và ghép nối 100% gói lệch thứ tự | Kiểm tra log ghép nối trên console |
| **RST Response** | Phản hồi RST < 10ms khi gõ port đóng | Chạy script Python `test_rst.py` |
| **RFC 5681 Reno** | Kích hoạt Fast Retransmit khi nhận 3 Dup-ACK | Bắt gói tin Wireshark phân tích |
| **BBR Throughput** | Nhanh hơn Reno ít nhất 1.5x - 2x khi mất gói 10% | Lệnh benchmark curl đo thời gian tải |
| **State Machine Stability**| Vượt qua 100% kịch bản kiểm thử FSM | Chạy bộ testbed tự động Python/Scapy |
