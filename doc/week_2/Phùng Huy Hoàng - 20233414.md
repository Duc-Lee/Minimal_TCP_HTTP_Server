# TIẾN TRÌNH TUẦN 2 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai: Hàng Đợi Gói Lệch Thứ Tự (`ooo_queue`), Phản Hồi TCP RST Chuẩn RFC 793 & Bộ Kiểm Thử Tự Động FSM State Tests

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 2 (Week 2)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục tiêu Tuần 2 (Thành viên 2)

Dựa theo kế hoạch phân công phối hợp kỹ thuật tại `plans/weeks/week_2.md` và `plans/members/member_2_tv2.md`:
1. **Triển khai Hàng đợi Out-of-Order (`ooo_queue`):** Giữ lại các segment TCP đến sớm/lệch thứ tự trong phạm vi Receive Window thay vì vứt bỏ; tự động ghép nối và đẩy dữ liệu lên socket khi nhận được gói tin bị thiếu, đồng thời phản hồi Duplicate ACK tương ứng.
2. **Hiện thực chuẩn phản hồi TCP RST (RFC 793):** Tự động tạo và gửi gói tin có cờ `RST` (kèm sequence/ack number chuẩn RFC 793 Page 36) khi nhận được gói tin gửi tới một cổng TCP không có tiến trình lắng nghe (closed port) hoặc kết nối không tồn tại.
3. **Quản lý tài nguyên & Dọn dẹp sạch sẽ:** Đảm bảo toàn bộ mbuf trong `ooo_queue` được giải phóng khi đóng kết nối, nhận RST hoặc thu hồi PCB trong `TIME_WAIT`.
4. **Xây dựng bộ kiểm thử tự động máy trạng thái (`tests/state_tests.py`):** Viết script Python tự động kiểm tra 5 kịch bản cốt lõi (3-Way Handshake, Closed Port RST, Half-Close CLOSE_WAIT, Stream Fragmentation, và 1MB Large File Transfer).

---

## 2. Hiện trạng đầu tuần

* **Vấn đề vứt bỏ gói lệch thứ tự:** Ở Tuần 1, trong `kernel/tcp_input.c`, nhánh `TCP_ESTABLISHED` chỉ chấp nhận gói in-order (`seq == pcb->rcv_nxt`). Bất kỳ segment nào đến lệch thứ tự (Seq > `rcv_nxt`) đều bị drop thẳng tay (`mbuffree(m)`). Khi mạng có tỉ lệ rớt gói 10%, phía gửi phải chờ timeout RTO và truyền lại toàn bộ các gói sau đó, gây lãng phí băng thông nghiêm trọng.
* **Chưa có phản hồi RST ở cổng đóng:** Khi nhận gói tin tới cổng chưa mở (ví dụ cổng 81), `tcp_lookup()` trả về `0`, kernel chỉ âm thầm hủy gói mà không gửi phản hồi RST, khiến máy đối tác bị treo kết nối (hang/timeout) thay vì nhận diện `Connection Refused` tức thì.
* **Rò rỉ PCB sau `TIME_WAIT`:** Trong `tcp_timer_process()`, khi trạng thái `TCP_TIME_WAIT` hết hạn, kernel chỉ gán `pcb->state = TCP_CLOSED` nhưng không thu hồi PCB về `free_list`, làm cạn kiệt bảng PCB sau nhiều phiên làm việc.

---

## 3. Chi Tiết Triển Khai Kỹ Thuật

### 3.1. Hàng đợi Out-of-Order Queue (`ooo_queue`)
* **Mở rộng cấu trúc dữ liệu (`kernel/net.h`):**
  - Bổ sung trường `uint32 seq;` vào `struct mbuf` để lưu trữ trực tiếp sequence number bắt đầu của segment.
  - Thêm con trỏ `struct mbuf *ooo_queue;` vào cấu trúc `struct tcp_pcb`.
* **Thuật toán chèn có thứ tự (`tcp_ooo_insert` trong `kernel/tcp_input.c`):**
  - Giới hạn dung lượng hàng đợi tối đa `TCP_OOO_MAX = 16` segment để bảo vệ bộ đệm `mbuf` kernel không bị tràn ngập khi gặp luồng dữ liệu bất thường.
  - Duyệt danh sách và chèn gói tin mới vào vị trí phù hợp theo thứ tự `seq` tăng dần.
  - Loại bỏ các gói tin bị trùng lặp sequence (`seq == curr->seq`).
* **Thuật toán ghép nối và xả dữ liệu tích lũy:**
  - Khi segment bị thiếu đến nơi (`seq == pcb->rcv_nxt`), dữ liệu của gói này được chuẩn bị đưa lên socket.
  - Ngay lập tức duyệt qua `pcb->ooo_queue`:
    * Bỏ qua các segment cũ đã nằm hoàn toàn trong quá khứ (`q->seq_end <= pcb->rcv_nxt`).
    * Nếu segment kế tiếp khớp chính xác với `pcb->rcv_nxt` hoặc có phần đầu trùng lặp, cắt phần trùng lặp (`mbufpull`), rút khỏi `ooo_queue`, cộng dồn `pcb->rcv_nxt += q->len` và đưa vào danh sách xả (`drain_list`).
    * Dừng lại ngay khi gặp khoảng trống dữ liệu (hole).
  - Đẩy toàn bộ các segment liên tục lên socket qua `sock_push_data()` và gửi **một ACK tích lũy duy nhất** mang giá trị `rcv_nxt` mới nhất.
  - Khi nhận gói out-of-order, lập tức gửi Duplicate ACK phản hồi `ack = pcb->rcv_nxt` để thông báo cho đối tác biết gói tin đang chờ.

### 3.2. Phản hồi TCP RST theo RFC 793 (`kernel/tcp_output.c`, `kernel/net.h`)
* Cài đặt hàm `tcp_send_rst(local_ip, local_port, remote_ip, remote_port, in_seq, in_ack, in_flags, in_datalen)`.
* Tuân thủ quy tắc sinh gói Reset chuẩn **RFC 793 Trang 36**:
  - Nếu gói tin đến có cờ `ACK`: Gói RST thiết lập `seq = in_ack`, cờ `TCP_FLAG_RST` (không bật ACK).
  - Nếu gói tin đến không có cờ `ACK` (ví dụ gói SYN khởi tạo vào cổng đóng): Gói RST thiết lập `seq = 0`, `ack = in_seq + in_datalen + (SYN/FIN ? 1 : 0)`, và bật cặp cờ `TCP_FLAG_RST | TCP_FLAG_ACK`.
  - Tuyệt đối không sinh RST nếu gói đến đã mang cờ RST để tránh vòng lặp phản hồi vô tận.
* Tích hợp vào `tcp_input()`: Khi `tcp_lookup()` không tìm thấy socket hoặc kết nối phù hợp (`!pcb`), gọi ngay `tcp_send_rst()` trước khi giải phóng `m`.

### 3.3. Dọn dẹp tài nguyên & Ngăn chặn rò rỉ bộ nhớ
* **Trong `kernel/tcp.c`:**
  - `tcp_pcb_alloc()`: Khởi tạo con trỏ `pcb->ooo_queue = 0`.
  - `tcp_pcb_free()`: Duyệt qua `pcb->ooo_queue` và gọi `mbuffree()` giải phóng toàn bộ các segment out-of-order chưa ráp nối trước khi hoàn trả PCB về `free_list`.
  - `tcp_timer_process()`: Khi hết hạn `TCP_TIME_WAIT`, gọi `tcp_pcb_free(pcb)` để rút PCB khỏi hash table và tái sử dụng cho các kết nối tiếp theo.
* **Trong `kernel/tcp_input.c`:** Khi nhận cờ RST, lập tức giải phóng sạch `pcb->ooo_queue` trước khi đưa trạng thái về `TCP_CLOSED`.

### 3.4. Cấu hình QEMU Port Forwarding (`Makefile`)
* Bổ sung chuyển tiếp cổng `hostfwd=tcp::20081-:81` trong `QEMUOPTS` tại `Makefile` để cho phép các kịch bản kiểm thử từ máy Host kết nối tới cổng đóng 81 trên guest xv6.

---

## 4. Kết Quả Kiểm Thử

### 4.1. Bộ kiểm thử tự động `tests/state_tests.py`

Bộ test được thực thi độc lập từ Host kiểm tra toàn diện máy trạng thái và các tính năng mới triển khai:

```text
=================================================================
      MINIMAL TCP/IP STACK AUTOMATED STATE TEST SUITE (WEEK 2)   
      Target: 127.0.0.1:20080 | RST Target: 127.0.0.1:20081
=================================================================
[*] Test 1: Standard 3-Way Handshake & HTTP GET /README...
[*] 3-Way Handshake completed in 0.10 ms
[PASS] Received 6840 bytes response correctly
[*] Test 2: RFC 793 TCP RST on unopened port 20081...
[PASS] Successfully closed/reset by peer via TCP RST in 50.32 ms
[*] Test 3: TCP Half-Close (FIN from Client, CLOSE_WAIT on xv6)...
[*] Sent FIN packet (Half-Close) via shutdown(SHUT_WR)
[PASS] Server successfully processed and returned 6840 bytes under Half-Close
[*] Test 4: Fragmented Stream Pipeline...
[PASS] Server successfully reassembled fragmented stream (6840 bytes)
[*] Test 5: Real 1MB File Download & SHA-256 Verification...
[*] Downloaded 1048576 bytes in 1.40 s (729.0 KB/s)
[*] SHA-256: 979a8e88a90ec16ed6141d34f4ce68086e38fb8870862c667bb37b1f22678328
[PASS] 1MB File Transfer: 100% complete and verified

-----------------------------------------------------------------
                     TEST SUMMARY REPORT                         
-----------------------------------------------------------------
  Test 1: 3-Way Handshake & GET          : PASSED
  Test 2: RFC 793 Closed Port RST        : PASSED
  Test 3: Half-Close (CLOSE_WAIT)        : PASSED
  Test 4: Fragmented Stream Pipeline     : PASSED
  Test 5: 1MB Large File Integrity       : PASSED

Final Score: 5/5 tests passed (100%)
=================================================================
```

### 4.2. Kiểm thử tải file 1 MB thật dưới điều kiện rớt gói 10%

Sau khi kích hoạt giả lập mất gói động `setloss 10`, tiến hành tải file `largefile.bin` (1,048,576 bytes):

| Mức Loss | Trạng thái | Kích thước nhận | Thời gian tải | Tốc độ trung bình | Kiểm tra tính toàn vẹn (SHA-256) |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **0%** | Thành công | 1,048,576 bytes | **1.40 s** | **729.0 KB/s** | Khớp 100% |
| **10%** | Thành công | 1,048,576 bytes | **4.75 s** | **215.4 KB/s** | Khớp 100% |

**Nhận xét:** Nhờ có `ooo_queue` lưu giữ lại các gói đến trước, tầng TCP không phải vứt bỏ dữ liệu hợp lệ khi có 1 gói bị mất giữa chừng. Khi gói bù đến nơi, toàn bộ chuỗi segment được ghép nối trơn tru, giúp thời gian tải 1 MB ở mức 10% loss đạt **4.75 giây** (đáp ứng trọn vẹn tiêu chí KPI $\le 5$ giây của dự án).

### 4.3. Phân tích hiện tượng lỗi thực nghiệm: `[Errno 104] Connection reset by peer` & Cách khắc phục

* **Hiện tượng lỗi ghi nhận:**
  Khi chạy `python3 tests/state_tests.py` hoặc `curl` từ Terminal bên Host trong khi Terminal bên guest xv6 vừa nạp kernel xong:
  - `Test 2: RFC 793 TCP RST on unopened port 20081` -> **PASSED** (Nhận diện Reset trong 50ms).
  - Các bài Test 1, 3, 4, 5 và lệnh `curl -v http://127.0.0.1:20080/README` đồng loạt báo lỗi:  
    `[FAIL] Test Failed: [Errno 104] Connection reset by peer`  
    `curl: (56) Recv failure: Connection reset by peer`.

* **Phân tích nguyên nhân kỹ thuật gốc rễ:**
  1. Quan sát Terminal bên trái chạy xv6: Hệ thống vừa khởi động xong và dừng lại ở dấu nhắc lệnh shell:
     ```text
     init: starting sh
     $ _
     ```
     Tiến trình Web Server `httpd` **chưa hề được chạy** trong xv6.
  2. Do `httpd` chưa chạy, cổng 80 trong kernel xv6 chưa có socket nào đăng ký ở trạng thái `TCP_LISTEN` (`tcp_lookup` trả về `0`).
  3. Khi request từ Host gửi đến cổng 20080 (được QEMU chuyển tiếp vào cổng 80 của guest), hàm `tcp_input()` kiểm tra không có PCB nào khớp. Theo đúng cơ chế phản hồi chuẩn **RFC 793** vừa triển khai tại mục 3.2, kernel xv6 lập tức sinh gói tin có cờ `TCP_FLAG_RST` gửi ngược lại Host để từ chối kết nối.
  4. Hệ điều hành Host (Linux) nhận được gói TCP RST từ guest nên hủy kết nối ngay lập tức và ném ra ngoại lệ `[Errno 104] Connection reset by peer`.  
  *(Điều này đồng thời là minh chứng thực nghiệm khẳng định cơ chế gửi RST khi gặp cổng đóng của kernel xv6 hoạt động cực kỳ nhạy và chuẩn xác theo RFC 793)*.

* **Cách khắc phục triệt để:**
  1. **Khi chạy thủ công (2 Terminal riêng biệt):**  
     Tại cửa sổ Terminal xv6, nhấp chuột vào sau dấu nhắc lệnh `$` và gõ lệnh:
     ```text
     $ httpd
     ```
     Nhấn Enter để server in ra thông báo `Starting xv6 httpd server (HTTP/1.0) on port 80...`. Lúc này cổng 80 đã mở và sẵn sàng phục vụ, các bài test từ Terminal bên Host sẽ vượt qua 100%.
  2. **Giải pháp tự động hóa bằng runner (`tests/run_tests.py`):**  
     Để người kiểm thử không phải thao tác thủ công và tránh quên gõ lệnh `httpd`, dự án cung cấp script `tests/run_tests.py`. Script này sử dụng `subprocess.Popen` khởi chạy QEMU, tự động điều khiển luồng `stdin` gửi lệnh `httpd\n` vào shell xv6, chờ server lắng nghe rồi mới kích hoạt `state_tests.py`.
  3. **Bổ sung gợi ý chẩn đoán trong `tests/state_tests.py`:**  
     Trong mã nguồn `tests/state_tests.py`, đã bổ sung khối bắt ngoại lệ `ConnectionResetError` trên cổng 20080 và in ra thông báo nhắc nhở trực quan: *"Cổng 20080 gửi RST từ chối kết nối. Hãy đảm bảo bạn đã gõ 'httpd' trong xv6 hoặc dùng 'python3 tests/run_tests.py'!"*.

---

## 5. Hướng Dẫn Cách Chạy Và Tái Hiện Kết Quả

### Cách 1: Chạy tự động trọn gói từ Host (Khuyên dùng)
Bộ kịch bản `tests/run_tests.py` sẽ tự khởi động QEMU, nạp kernel, gửi lệnh bật `httpd` và kích hoạt toàn bộ 5 bài test:
```bash
# Biên dịch kernel và disk image
make clean && make -j$(nproc)
make fs.img

# Chạy bộ test tự động
python3 tests/run_tests.py
```

### Cách 2: Chạy thủ công trên 2 cửa sổ terminal

**Terminal 1 (Chạy xv6 Web Server):**
```bash
make qemu
# Trong shell xv6, gõ:
httpd
```

**Terminal 2 (Thực thi kiểm thử từ Host):**
```bash
# 1. Chạy bộ kiểm thử tự động máy trạng thái FSM:
python3 tests/state_tests.py --host 127.0.0.1 --port 20080 --rst-port 20081

# 2. Tải thử file tĩnh văn bản:
curl -v http://127.0.0.1:20080/README

# 3. Tải file 1 MB thật và đối chứng mã băm SHA-256:
curl http://127.0.0.1:20080/largefile.bin -o dl_test.bin
sha256sum dl_test.bin user/largefile.bin
```

**Kiểm thử rớt gói 10% động:**
```bash
# Trong shell xv6:
setloss 10
setloss status
httpd

# Từ Host:
curl --max-time 30 http://127.0.0.1:20080/largefile.bin -o dl_loss.bin
sha256sum dl_loss.bin user/largefile.bin
```

---

## 6. Tồn Đọng & Kế Hoạch Tuần 3

* **Tồn đọng:** Hiện tại kích thước Receive Window `rcv_wnd` đang cố định ở 4096 bytes (tương đương ~3 MSS). Ở Tuần 3, khi phát triển thuật toán kiểm soát tắc nghẽn (TCP Tahoe / Reno), cần nâng cấp cơ chế cập nhật Dynamic Window và Congestion Window (`cwnd`).
* **Kế hoạch Tuần 3:**
  - Cài đặt thuật toán kiểm soát tắc nghẽn TCP Reno theo RFC 5681 (`kernel/tcp_cc.c`): Slow Start, Congestion Avoidance, Fast Retransmit khi nhận 3 Dup-ACKs, Fast Recovery.
  - Tích hợp Quy tắc Karn (RFC 6298) vào việc đo đạc RTT, không lấy mẫu RTT từ các segment truyền lại.

---

## 7. Danh Sách Tệp Đã Sửa Đổi / Bổ Sung

| Tệp tin | Thao tác | Mô tả chi tiết |
| :--- | :---: | :--- |
| `kernel/net.h` | Sửa đổi | Bổ sung `seq` vào `struct mbuf`, thêm con trỏ `ooo_queue` vào `struct tcp_pcb`, khai báo hàm `tcp_send_rst()`. |
| `kernel/mbuf.c` | Sửa đổi | Khởi tạo trường `m->seq = 0` trong `mbufalloc()` và `mbufalloc_wait()`. |
| `kernel/tcp.c` | Sửa đổi | Khởi tạo `ooo_queue = 0`, giải phóng `ooo_queue` trong `tcp_pcb_free()`, thu hồi PCB khi hết hạn `TIME_WAIT`. |
| `kernel/tcp_output.c` | Sửa đổi | Hiện thực hàm `tcp_send_rst()` sinh gói RST/ACK chuẩn RFC 793 Page 36. |
| `kernel/tcp_input.c` | Sửa đổi | Cài đặt `tcp_ooo_insert()`, xử lý gói out-of-order, ráp nối dữ liệu lên socket khi có gói bù, gửi RST khi `!pcb`. |
| `Makefile` | Sửa đổi | Bổ sung cổng chuyển tiếp `hostfwd=tcp::20081-:81` phục vụ kiểm thử RST. |
| `tests/state_tests.py` | Tạo mới | Bộ kịch bản kiểm thử tự động 5 ca kiểm thử FSM bằng Python socket chuẩn. |
| `tests/run_tests.py` | Tạo mới | Script chạy tích hợp đầu-cuối khởi chạy QEMU, httpd và testbed tự động. |
| `doc/week_2/Phùng Huy Hoàng - 20233414.md` | Hoàn thiện | Báo cáo tiến trình Tuần 2 theo chuẩn tài liệu dự án. |
