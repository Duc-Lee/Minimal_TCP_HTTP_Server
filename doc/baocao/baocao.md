# PROGRESS REPORT
**Course:** Operating Systems - 20252  
**Project No:** [Bổ sung số nhóm / Project No]  
**Project title:** Minimal TCP/IP Stack & HTTP Server in xv6-riscv (64-bit)  
**Design thinking page:** [Bổ sung link Figma / Whiteboard]  
**Task management page:** [Bổ sung link Trello / Jira]  
**Members:**  
1. Phùng Huy Hoàng - 20233414  
2. Nguyễn Vũ Hiệp - 20233392  
3. Lê Anh Đức - 20233324  

**Reported working period:**  
from: [Bổ sung ngày bắt đầu] to: [Bổ sung ngày kết thúc]

---

## Summary of latest progress

Trong tuần đầu tiên, nhóm em đã hoàn thành giai đoạn khởi tạo, thiết kế kiến trúc và triển khai thành công toàn bộ khung cốt lõi (Giai đoạn 1 - MVP) của hệ thống ngăn xếp mạng TCP/IP tối giản (Minimal TCP/IP Stack) cùng Web Server HTTP/1.0 nhúng trực tiếp trong kernel hệ điều hành giáo dục **xv6-riscv** (64-bit). Trọng tâm của tuần này tập trung vào 3 hạng mục nền tảng:
1. Xây dựng Driver mạng cho card NIC Intel 82540EM (E1000) trên QEMU cùng bộ đệm `mbuf` và bộ lọc giả lập tỉ lệ rớt gói 10%.
2. Hiện thực các tầng giao thức mạng: Ethernet II (L2), ARP Resolution & Dynamic Cache, IPv4 routing & RFC 1071 Internet Checksum (L3), và TCP FSM 11 trạng thái với Sliding Window, Retransmission Queue và RTO dynamic calculation (L4).
3. Tích hợp BSD Socket API vào VFS xv6 (`FD_SOCK`) và xây dựng user-space HTTP/1.0 Server (`httpd`) có khả năng truyền tải thành công tệp dữ liệu 1 MB (`/file1mb`) về máy Host trong điều kiện mạng rớt gói 10%, mã băm SHA-256 khớp 100%.

Bên cạnh đó, các thành viên khác trong nhóm cũng đã hoàn thành việc rà soát kiến trúc để chuẩn bị cho các giai đoạn tiếp theo: kế hoạch tối ưu hóa chống Deadlock / rò rỉ bộ nhớ / RTO calibration (Giai đoạn 2), cũng như phương án nâng cấp hệ thống tệp Inode Doubly-Indirect để phục vụ đọc file 1 MB thật từ đĩa (Giai đoạn 3).

Tiến độ hiện tại của dự án bám sát và thậm chí vượt kế hoạch đề ra của Tuần 1. Xét trên tổng thể 6 tuần, nhóm đánh giá **cumulative progress đạt khoảng 25–30%**, đã nghiệm thu đầy đủ Giai đoạn 1 (Core Network Stack & HTTP Server) và sẵn sàng tích hợp các module nâng cao.

---

## Review of technical activities

* **Activity 1: Hiện thực Driver NIC Intel 82540EM (E1000) và cơ chế cấp phát bộ đệm `mbuf`**  
  Nhóm đã tiến hành khởi tạo phần cứng card mạng Intel 82540EM theo chuẩn PCI; thiết lập DMA Ring Buffer với 16 TX descriptors và 16 RX descriptors; cấu hình ngắt phần cứng nhận gói (`RXT0`). Xây dựng cấu trúc quản lý bộ đệm gói tin `struct mbuf` và tích hợp bộ lọc ngẫu nhiên LCG trong driver nhằm giả lập tỉ lệ rớt gói 10% cho các gói tin IP/TCP để kiểm thử tính chịu lỗi của tầng giao vận.

* **Activity 2: Xây dựng tầng mạng L2/L3 (Ethernet II, ARP Resolution, IPv4 & Checksum RFC 1071)**  
  Đóng gói và bóc tách Ethernet frame; triển khai giao thức ARP với bảng ARP cache động, tự động gửi ARP Request broadcast và xử lý ARP Reply; cài đặt xử lý IPv4 Header (TTL, kiểm tra địa chỉ đích `local_ip`); cài đặt thuật toán tính bù 1 Internet Checksum chuẩn RFC 1071 áp dụng chung cho cả IPv4 và TCP Pseudo-Header.

* **Activity 3: Triển khai tầng giao vận TCP (RFC 793), Socket VFS & HTTP Server**  
  Thiết kế máy trạng thái hữu hạn TCP FSM 11 trạng thái; cơ chế bắt tay 3 bước (3-Way Handshake) và đóng kết nối 4 bước; thuật toán Sliding Window điều khiển luồng; hàng đợi truyền lại `retransmit_q` kết hợp tính toán RTO động theo Jacobson/Karels (RFC 6298) và Fast Retransmit (RFC 5681). Tích hợp các system call chuẩn BSD Socket (`socket`, `bind`, `listen`, `accept`, `connect`) vào Virtual File System dưới dạng `FD_SOCK`. Viết tiến trình người dùng `httpd` phục vụ request HTTP/1.0 tĩnh và endpoint kiểm thử luồng nhị phân 1 MB.

---

## Personal contribution

* **Member 1: Phùng Huy Hoàng - 20233414**  
  Đảm nhiệm rà soát độ tin cậy của kernel mạng, khảo sát hiện tượng cạn kiệt bộ đệm và lên phương án gia cố cho Giai đoạn 2: phân tích lỗi tiềm ẩn Deadlock trong `sock_write` khi hết `mbuf`, thiết kế cơ chế đánh thức `wakeup` trong `mbuffree`, thiết kế system call cấu hình động tỉ lệ rớt gói (`sys_set_loss_rate`), và chuẩn hóa nhịp xung RTO timer.  
  *Output:* Báo cáo phân tích nguy cơ Deadlock và kiến trúc RTO timer; tài liệu thiết kế system call điều khiển packet loss.

* **Member 2: Nguyễn Vũ Hiệp - 20233392**  
  Đảm nhiệm khảo sát hệ thống tệp xv6 và thiết kế phương án nâng cấp Inode cho Giai đoạn 3: phân tích giới hạn kích thước tệp ~268 KB do chỉ có Singly-Indirect block; thiết kế cấu trúc `struct dinode` / `struct inode` hỗ trợ Doubly-Indirect Block nâng giới hạn lên ~65.8 MB; rà soát tính căn chỉnh bộ nhớ đĩa `sizeof(dinode) = 64 bytes`; chuẩn bị phương án cập nhật `bmap()` và `mkfs`.  
  *Output:* Tài liệu thiết kế cấu trúc Inode Doubly-Indirect; kế hoạch chỉnh sửa `kernel/fs.c` và `mkfs/mkfs.c`.

* **Member 3: Lê Anh Đức - 20233324**  
  Chủ trì triển khai trọn vẹn toàn bộ Giai đoạn 1 (Cốt lõi MVP): cài đặt driver E1000, cấu trúc `mbuf`, Ethernet II, ARP, IPv4, Checksum RFC 1071, TCP FSM (11 trạng thái), Sliding Window, Retransmission RTO, Socket VFS (`FD_SOCK`), và Web Server HTTP/1.0 (`httpd`). Trực tiếp chạy demo, đo đạc truyền tải thành công tệp 1 MB từ guest xv6 về Host Linux qua curl với tỉ lệ rớt gói 10% và xác thực SHA-256 trùng khớp 100%.  
  *Output:* Toàn bộ source code kernel network stack (`kernel/e1000.c`, `eth.c`, `arp.c`, `ip.c`, `checksum.c`, `tcp.c`, `tcp_input.c`, `tcp_output.c`, `tcp_timer.c`, `sock.c`, `sysnet.c`, `ringbuf.c`) và ứng dụng `user/httpd.c`; kết quả thực nghiệm và hash SHA-256 tệp 1 MB.

---

## Developed code review

### a. Algorithmic development
* **Internet Checksum bù 1 (RFC 1071):** Triển khai tại `kernel/checksum.c`. Thuật toán tích lũy các từ 16-bit big-endian vào biến 32-bit `sum`, thực hiện gập các bit tràn `sum = (sum & 0xffff) + (sum >> 16)` cho đến khi triệt tiêu phần tràn, sau đó đảo bit `~sum`. Thuật toán đảm bảo tính chính xác tuyệt đối cho cả Header IPv4 lẫn TCP Pseudo Header.
* **Máy trạng thái TCP FSM 11 trạng thái & Bắt tay 3 bước:** Triển khai tại `kernel/tcp.c`, `kernel/tcp_input.c`, `kernel/tcp_output.c`. Xử lý chuyển dịch trạng thái từ `LISTEN` $\rightarrow$ `SYN_RCVD` $\rightarrow$ `ESTABLISHED`, clone PCB khi nhận SYN, cấp phát ISN (Initial Sequence Number) động và quản lý hàng đợi kết nối backlog.
* **Định thời truyền lại RTO (Jacobson/Karels, RFC 6298) & Exponential Backoff:** Theo dõi Round-Trip Time (RTT), tính Smoothed RTT ($SRTT$) và biến thiên ($RTTVAR$). Khi xảy ra quá hạn (Timeout) mà chưa nhận được ACK tương ứng trong `retransmit_q`, kích hoạt truyền lại segment đầu hàng đợi và nhân đôi RTO (`rto = min(rto * 2, 2000)`).
* **Điều khiển luồng Sliding Window:** Giới hạn số lượng segment "in-flight" (chưa được ACK) không vượt quá dung tích Receive Window của đối phương và giới hạn hàng đợi kernel, tự động đưa process vào trạng thái ngủ khi nghẽn và đánh thức khi có ACK mới.

### b. Source code enhanced and/or revised
* **Khởi tạo và cấu hình phần cứng mạng:** Hiện thực mới hoàn toàn các file `kernel/e1000.c`, `kernel/e1000_dev.h`, `kernel/mbuf.c` để quản lý I/O bộ nhớ và DMA ring.
* **Xử lý giao thức mạng L2/L3:** Hiện thực mới `kernel/eth.c`, `kernel/arp.c`, `kernel/ip.c`, `kernel/checksum.c`.
* **Giao thức L4 và Socket Interface:** Hiện thực `kernel/tcp.c`, `kernel/tcp_input.c`, `kernel/tcp_output.c`, `kernel/tcp_timer.c`, `kernel/ringbuf.c`, `kernel/sock.c`, `kernel/sysnet.c`.
* **Tích hợp Kernel Core:**
  - Sửa `kernel/file.h`, `kernel/file.c`, `kernel/sysfile.c` để bổ sung kiểu tệp `FD_SOCK`, định tuyến các lời gọi `read()` và `write()` sang socket layer (`sock_read()`, `sock_write()`).
  - Cập nhật `kernel/syscall.h`, `kernel/syscall.c` bổ sung các mã system call: `SYS_socket`, `SYS_bind`, `SYS_listen`, `SYS_accept`, `SYS_connect`.
  - Cập nhật `kernel/main.c`, `kernel/trap.c` để gọi khởi tạo mạng `pci_init()` và bắt ngắt thiết bị E1000.
* **User-space:** Viết daemon `user/httpd.c` lắng nghe cổng 80, bóc tách `GET /path HTTP/1.0`, mở tệp và gửi trả HTTP Response cùng dữ liệu.

### c. Testing result
* **Kiểm thử biên dịch và khởi động:** Hệ điều hành xv6 biên dịch sạch sẽ (`make clean && make -j$(nproc)`), QEMU khởi chạy nhận diện thành công thiết bị PCI Intel E1000 (`vendor/dev=0x8086/0x100e`) và bật ngắt mạng bình thường.
* **Kiểm thử HTTP GET văn bản tĩnh:** Khởi chạy `httpd` trên xv6, từ máy host thực hiện `curl -v http://127.0.0.1:20080/README`, server phản hồi mã `HTTP/1.0 200 OK` và nội dung file README chính xác 100%.
* **Kiểm thử độ tin cậy tải file 1 MB dưới điều kiện rớt gói 10%:**
  - Kích hoạt mô phỏng drop gói 10% tại E1000 transmit.
  - Chạy `curl --max-time 60 http://127.0.0.1:20080/file1mb -o downloaded_1mb.bin`. Tệp 1 MB (1,048,576 bytes) được truyền tải trọn vẹn, cơ chế truyền lại TCP xử lý thành công tất cả các gói bị mất.
  - So khớp mã băm SHA-256 với tệp kỳ vọng `expected_1mb.bin`: Cả 2 file đều đạt mã băm `c4e1ab33a4d2f0d7be4e6be6c387096da51a28bad003f71309bb774f75139111`, chứng minh tầng TCP truyền nhận dữ liệu tin cậy tuyệt đối, không mất mát, không xáo trộn thứ tự.

---

## TODO

### a) List of remaining issues
1. **Lỗi Deadlock mbuf khi tải liên tục:** Hàm `mbuffree()` chưa gọi `wakeup()`, khi toàn bộ 128 `mbuf` bị giữ bởi `retransmit_q`, tiến trình `sock_write()` có thể bị sleep vĩnh viễn nếu không có cơ chế timeout/reclaim an toàn.
2. **Metadata `seq_end` đang lưu vào mảng dữ liệu:** Cần trường riêng trong `struct mbuf` thay vì ghi đè vào 4 byte đầu của `buf`.
3. **Tỉ lệ rớt gói bị hardcode:** Tỉ lệ rớt gói 10% đang cố định trong code C của `kernel/e1000.c`, cần mở rộng thành syscall cấu hình động từ user-space.
4. **Giới hạn tệp tin xv6 chỉ ~268 KB:** Web Server hiện phục vụ `/file1mb` bằng luồng thuật toán sinh byte ảo; hệ thống tệp xv6 chưa hỗ trợ Doubly-Indirect block nên chưa thể lưu file 1 MB thật trên đĩa.
5. **Kịch bản kiểm thử regression:** Script `test-xv6.py usertests` gặp lỗi BrokenPipe do cấu hình môi trường QEMU chuyển tiếp cổng mạng.

### b) List of tasks to carry out in the next period (Tuần 2)
* **Task 1 (Hoàng):** Khắc phục triệt để nguy cơ Deadlock mbuf bằng cách bổ sung `wakeup(&mbuf_pool)` trong `mbuffree()` và cài đặt `mbufalloc_wait()`. Bổ sung trường `seq_end` vào `struct mbuf`.
* **Task 2 (Hoàng):** Cài đặt 2 system call `set_loss_rate(int)` và `get_loss_rate()` cùng chương trình `user/setloss` để điều chỉnh linh hoạt tỉ lệ rớt gói lúc chạy. Chuẩn hóa RTO Timer về tần số 10 Hz.
* **Task 3 (Hiệp):** Nâng cấp Inode trong `kernel/fs.h` và `kernel/fs.c` sang kiến trúc Doubly-Indirect Block (11 direct + 1 single + 1 double-indirect), nâng trần file lên ~65.8 MB; đồng bộ công cụ `mkfs/mkfs.c`.
* **Task 4 (Đức + Hiệp):** Tạo tệp 1 MB thật trên đĩa xv6, cập nhật `user/httpd.c` đọc trực tiếp từ đĩa bằng `open()` và `read()` thay cho sinh dữ liệu ảo.
* **Task 5 (Cả nhóm):** Thực hiện đo đạc benchmark so sánh hiệu năng truyền tải giữa các mức tỉ lệ rớt gói (0%, 5%, 10%, 20%) và hoàn thiện báo cáo tuần 2.

### c) Milestone
**Milestone tiếp theo (Kết thúc Giai đoạn 2 & Giai đoạn 3):** Toàn bộ ngăn xếp mạng hoạt động ổn định không xảy ra hiện tượng cạn kiệt bộ đệm (0 Deadlock), tỉ lệ rớt gói có thể cấu hình động qua lệnh `setloss`, và Web Server có khả năng phân phối tệp nhị phân 1 MB thật được lưu trữ trực tiếp trên đĩa xv6 với mã băm SHA-256 trùng khớp 100%.

---

## Minh chứng kết quả thực nghiệm và quản lý công việc

### 1. Kết quả thực nghiệm của thành viên Lê Anh Đức (image_duc)

#### a. Khởi động xv6, nạp Driver E1000 và chạy Web Server HTTP/1.0
Kernel nhận diện thành công card mạng PCI E1000 (`vendor/dev=0x100e8086`), cấu hình DMA Ring và tiến trình `httpd` lắng nghe tại port 80:

![Khởi động xv6 và chạy httpd](image_duc/Screenshot%20from%202026-10-24%2018-11-20.png)

#### b. Kiểm thử gửi HTTP Request lấy file tĩnh (`/README`) từ Host
Gửi request qua `curl -v http://127.0.0.1:20080/README`, server phản hồi HTTP/1.0 200 OK và in toàn bộ nội dung file README của dự án:

![HTTP GET /README thành công](image_duc/Screenshot%20from%202026-10-24%2018-12-01.png)

#### c. Kiểm thử truyền tải tệp 1 MB (`/file1mb`) với bộ lọc rớt gói 10%
Thực hiện tải file 1,048,576 bytes từ guest xv6 về Host Linux thông qua `curl` dưới điều kiện card mạng liên tục drop ngẫu nhiên 10% gói IP/TCP:

![Tải thành công file 1MB](image_duc/Screenshot%20from%202026-10-24%2018-13-16.png)

#### d. Xác thực tính toàn vẹn dữ liệu bằng mã băm SHA-256
Tạo file kỳ vọng `expected_1mb.bin` và so sánh mã băm SHA-256 của file tải về: Cả hai mã băm trùng khớp tuyệt đối (`c4e1ab33a4d2f0d7be4e6be6c387096da51a28bad003f71309bb774f75139111`), khẳng định tầng TCP kiểm soát luồng và truyền lại gói tin chính xác 100%:

![So khớp mã băm SHA-256 khớp 100%](image_duc/Screenshot%20from%202026-10-24%2018-13-52.png)

#### e. Kiểm tra Regression Tests với `test-xv6.py usertests`
Ghi nhận kết quả thực thi script kiểm thử hồi quy `test-xv6.py usertests` trên môi trường:

![Kết quả chạy test-xv6.py](image_duc/Screenshot%20from%202026-10-24%2018-14-59.png)

---

### 2. Kết quả kiểm thử và đóng góp của thành viên Phùng Huy Hoàng
[bo sung anh]

---

### 3. Kết quả kiểm thử và đóng góp của thành viên Nguyễn Vũ Hiệp
[bo sung anh]

---

### 4. Bảng quản lý tiến độ công việc trên Trello
[bo sung anh]
