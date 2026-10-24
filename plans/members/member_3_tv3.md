# KẾ HOẠCH HÀNH ĐỘNG CHI TIẾT - THÀNH VIÊN 3 (TV3)
## Chuyên môn: Tầng Ứng Dụng, Hệ Thống Tệp 1 MB Thật, Web Server & Trực Quan Hóa

---

## 1. Thông Tin Thành Viên & Phạm Vi Kỹ Thuật

* **Định hướng kỹ thuật chính:** Tầng Ứng Dụng, Hệ Thống Tệp 1 MB Thật, Web Server & Trực Quan Hóa (phối hợp linh hoạt cùng nhóm).
* **Tỷ trọng đóng góp:** 33.3% khối lượng toàn bộ dự án.
* **Danh mục mã nguồn phụ trách chính:**
  * `kernel/fs.c`, `kernel/fs.h`, `mkfs/mkfs.c`: Nâng cấp cấu trúc hệ thống tệp đĩa xv6 (Doubly-Indirect Block) để chứa file 1 MB thật.
  * `kernel/sock.c`: Giao tiếp VFS Socket, quản lý trạng thái file descriptor mạng và điều khiển ngủ/thức của tiến trình.
  * `kernel/ringbuf.c`: Bộ đệm vòng (Ring Buffer) đa luồng an toàn cho dữ liệu luồng byte của socket.
  * `kernel/sendfile.c`: Hiện thực lời gọi hệ thống Zero-Copy `sys_sendfile`.
  * `user/httpd.c`: Web Server HTTP/1.1 kiến trúc Pre-fork Worker Pool, tương thích trình duyệt web.
  * `user/netstat.c`: Ứng dụng dòng lệnh liệt kê các kết nối mạng đang hoạt động.
  * `user/tcptrace.c`: Dashboard thời gian thực vẽ biểu đồ thanh ASCII về trạng thái TCP trực tiếp trên console xv6.
  * `scripts/*.sh`: Bộ script tự động hóa chạy demo và đo lường benchmark trên máy Host.

---

## 2. Lộ Trình 6 Tuần Chi Tiết Của Thành Viên 3

### TUẦN 1: Nâng Cấp Hệ Thống Tệp 1 MB Thật (Doubly-Indirect) & Đọc Đĩa Thật
* **Mục tiêu kỹ thuật:** Chấm dứt việc sinh dữ liệu giả trong RAM; lưu trữ và đọc file 1 MB thật từ hệ thống tệp của xv6.
* **Nhiệm vụ cụ thể:**
  1. **Nâng cấp Hệ thống tệp xv6 (`kernel/fs.h`, `kernel/fs.c`, `mkfs/mkfs.c`):**
     - Cấu trúc inode mặc định của xv6 chỉ có 12 Direct blocks và 1 Singly-indirect block, giới hạn dung lượng file tối đa là:
       $$(12 + 256) \times 1024\text{ bytes} \approx 268\text{ KB}$$
     - Cải tạo inode: Thay thế 1 direct block bằng **Doubly-Indirect Block**, cho phép trỏ tới một block chứa 256 địa chỉ của các indirect block tiếp theo. Dung lượng tối đa nâng lên:
       $$(11 + 256 + 256 \times 256) \times 1024\text{ bytes} \approx 65.8\text{ MB}$$
     - Tăng kích thước phân vùng đĩa `FSSIZE = 20000` trong `kernel/param.h`.
     - Đồng bộ hóa logic cấp phát block trong hàm `bmap()` tại `kernel/fs.c` và công cụ tạo đĩa `mkfs/mkfs.c`.
  2. **Tạo file dữ liệu 1 MB thật (`largefile.bin`):**
     - Tạo file nhị phân ngẫu nhiên dung lượng chính xác $1,048,576\text{ bytes}$ và nhúng vào `fs.img` qua Makefile.
  3. **Chỉnh sửa Web Server `user/httpd.c`:**
     - Xóa bỏ hoàn toàn hàm mock sinh byte ảo `/file1mb`.
     - Chuyển sang mở file thật: `fd = open("largefile.bin", O_RDONLY)` và đọc từng block $1\text{ KB}$ từ đĩa gửi qua socket.
  4. **Tích hợp `mbufalloc_wait()` vào `sock_write()`:**
     - Đã hoàn thành tại Phase 2: Đảm bảo khi file 1 MB được đẩy liên tục vào socket, tiến trình `httpd` không bị crash khi mbuf pool cạn kiệt.

---

### TUẦN 2: Xử Lý Lỗi Tầng Socket & Phục Hồi Khi Client Hủy Kết Nối
* **Mục tiêu kỹ thuật:** Đảm bảo tầng socket thông báo lỗi chính xác cho ứng dụng user-space khi gặp sự cố mạng.
* **Nhiệm vụ cụ thể:**
  1. **Xử lý tín hiệu ngắt kết nối trong `kernel/sock.c`:**
     - Khi nhận được cờ `RST` hoặc `FIN` từ TV2, hàm `sock_read()` phải trả về giá trị `0` (EOF) để báo hiệu cho ứng dụng dừng đọc.
     - Hàm `sock_write()` nếu phát hiện socket đã ở trạng thái `TCP_CLOSED` phải trả về mã lỗi `-1` (`EPIPE` / Connection Broken) thay vì treo vô hạn.
  2. **Cơ chế đóng Socket sạch sẽ:**
     - Hoàn thiện hàm `sock_close()`: Đảm bảo khi tiến trình gọi `close(sock_fd)`, hệ điều hành giải phóng đúng các trang bộ đệm ring buffer và hủy liên kết PCB tương ứng.

---

### TUẦN 3: Nâng Cấp Ring Buffer Đa Luồng & Thu Thập Thống Kê Kernel TCP
* **Mục tiêu kỹ thuật:** Cung cấp bộ đệm vòng tốc độ cao và mở API trích xuất thống kê TCP từ kernel.
* **Nhiệm vụ cụ thể:**
  1. **Tối ưu hóa `kernel/ringbuf.c`:**
     - Đảm bảo cơ chế đọc/ghi trên bộ đệm vòng là an toàn đa luồng (lock-free single-producer single-consumer hoặc dùng spinlock bảo vệ nghiêm ngặt).
  2. **Hiện thực Lời gọi hệ thống `sys_netstat`:**
     - Thêm syscall `sys_netstat(void *buf, int max_entries)` trong `kernel/sysnet.c`.
     - Duyệt danh sách các PCB đang hoạt động và copy các thông số: Địa chỉ IP nguồn/đích, Cổng, Trạng thái FSM, `cwnd`, `ssthresh`, `rtt` lên buffer user-space.
  3. **Xây dựng chương trình `user/netstat.c`:**
     - In ra bảng danh sách các kết nối mạng tương tự như lệnh `netstat -ant` trên Linux.

---

### TUẦN 4: Web Server Đa Kết Nối (Worker Pool) & Tương Thích Trình Duyệt Web
* **Mục tiêu kỹ thuật:** Cho phép mở trang web bằng trình duyệt thực tế (Chrome / Firefox) và phục vụ nhiều client cùng lúc.
* **Nhiệm vụ cụ thể:**
  1. **Kiến trúc Pre-forked Worker Pool trong `user/httpd.c`:**
     - Thay vì xử lý tuần tự từng client (Single-process model), tiến trình cha sau khi `bind()` và `listen()` sẽ gọi `fork()` sinh ra 4–8 tiến trình con (Worker processes).
     - Tất cả các worker cùng lắng nghe và cạnh tranh gọi `accept()` trên cùng một file descriptor lắng nghe (Mô hình kinh điển của Nginx/Apache).
     - Khi một client đang tải file 1 MB nặng, worker khác vẫn lập tức phục vụ các request mới từ client khác.
  2. **Tương thích Trình duyệt (Chrome / Firefox / Edge):**
     - Nâng kích thước buffer nhận HTTP request lên 2048 bytes để không bị tràn buffer do Request Header dài của trình duyệt.
     - Phân tích và xử lý route `/favicon.ico`: Trả về `204 No Content` hoặc dữ liệu ảnh icon thay vì báo lỗi `404 Not Found`.
  3. **Hỗ trợ HTTP/1.1 Tiêu chuẩn:**
     - Hỗ trợ header `Connection: keep-alive` cho phép tái sử dụng kết nối TCP cho nhiều tài nguyên (HTML, CSS, Image).
     - Trả về header `Content-Type` tương ứng với đuôi file (`text/html`, `image/png`, `application/octet-stream`).

---

### TUẦN 5: Đột Phá Hiệu Năng Zero-Copy `sys_sendfile` & Live ASCII Dashboard
* **Mục tiêu kỹ thuật:** Đạt điểm thưởng sáng tạo tối đa bằng tính năng Zero-Copy và bộ công cụ giám sát trực quan thời gian thực.
* **Nhiệm vụ cụ thể:**
  1. **Hiện thực Zero-Copy `sys_sendfile` (`kernel/sendfile.c`):**
     - Syscall: `sys_sendfile(int out_sock_fd, int in_file_fd, uint64 offset, int count)`.
     - **Cơ chế Zero-Copy:** Đọc trực tiếp block từ Buffer Cache đĩa qua hàm `bread()` trong `kernel/bio.c`, chép trực tiếp vào payload của `mbuf` và đẩy xuống card mạng E1000 mà **không copy dữ liệu lên vùng nhớ user-space** (tiết kiệm 2 lần copy dữ liệu và 2 lần chuyển đổi ngữ cảnh User-Kernel).
     - Đọc thanh ghi chu kỳ lệnh phần cứng RISC-V `rdcycle` trước và sau khi truyền để đo đạc chính xác số chu kỳ CPU tiết kiệm được.
  2. **Xây dựng Dashboard Thời Gian Thực `user/tcptrace.c`:**
     - Sử dụng mã điều khiển ANSI (Escape sequences) để xóa màn hình và tự động refresh mỗi 500ms.
     - Vẽ biểu đồ thanh ASCII (Bar chart) hiển thị trực quan kích thước cửa sổ `cwnd`, `ssthresh`, `RTO` và số lượng gói truyền lại ngay trên terminal xv6 khi client đang tải dữ liệu.

---

### TUẦN 6: Stress Test ApacheBench, Script Demo 1-Click & Báo Cáo
* **Mục tiêu kỹ thuật:** Kiểm chứng năng lực chịu tải đồng thời và đóng gói sản phẩm hoàn thiện.
* **Nhiệm vụ cụ thể:**
  1. **Kiểm thử chịu tải đồng thời với ApacheBench (`ab`):**
     - Chạy lệnh kiểm thử từ Host: `ab -n 200 -c 8 http://127.0.0.1:20080/index.html`.
     - Đảm bảo tỷ lệ phục vụ thành công đạt 100% không có kết nối nào bị timeout hoặc sập server.
  2. **Đóng gói 4 Script Demo 1-Click (`scripts/`):**
     - `demo1_core_1mb_loss10.sh`: Chạy kiểm thử cốt lõi (1 MB dưới mất gói 10% và xác minh SHA-256).
     - `demo2_reno_vs_bbr.sh`: Chạy so sánh hiệu năng giữa TCP Reno và Google BBR.
     - `demo3_sendfile_benchmark.sh`: So sánh chu kỳ CPU và tốc độ giữa `read/write` truyền thống và `sendfile`.
     - `demo4_ab_concurrency.sh`: Bắn 50 kết nối đồng thời và xuất bảng thống kê độ trễ.
  3. **Hoàn thiện Báo cáo & Video diễn tập:**
     - Phối hợp với TV1 và TV2 tổng hợp báo cáo kỹ thuật hoàn chỉnh và diễn tập thuyết trình demo.

---

## 3. Tiêu Chí Nghiệm Thu (KPIs) Của TV3

| Hạng mục | Chỉ tiêu định lượng | Phương pháp đo lường |
| :--- | :--- | :--- |
| **File System 1 MB** | Đọc file $1,048,576\text{ bytes}$ từ đĩa thật | Kiểm tra inode doubly-indirect trong `fs.c` |
| **Multi-worker Concurrency**| Phục vụ đồng thời ≥ 8 kết nối không nghẽn | Dùng lệnh `ab -n 200 -c 8` trên Host |
| **Browser Compatibility** | Tải mượt mà trên Google Chrome / Firefox | Mở trình duyệt truy cập `http://localhost:20080` |
| **Zero-Copy CPU Savings** | Giảm ≥ 25% chu kỳ CPU so với read/write | Đo bằng lệnh RISC-V `rdcycle` |
| **Live Observability** | Dashboard cập nhật nhịp nhàng mỗi 500ms | Chạy `tcptrace` trên console xv6 |
