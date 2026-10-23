# KẾ HOẠCH PHỐI HỢP TUẦN 1 (WEEK 1 PLAN)
## Chủ đề: Khảo Sát Mã Nguồn, Vá Deadlock Mbuf, Chuẩn Hóa Timer & Hệ Thống Tệp 1 MB Thật

---

## 1. Trọng Tâm & Mục Tiêu Kỹ Thuật

* **Bối cảnh:** Bộ khung sơ khai của Phase 1 chứa nhiều lỗi ngầm nghiêm trọng: rò rỉ bộ nhớ ARP, deadlock khi cạn kiệt mbuf pool, RTO timer bị chậm gấp 10 lần (do tính sai tần số tick), và dữ liệu 1 MB chỉ là chuỗi số sinh ảo trong RAM mà chưa đọc từ đĩa thật.
* **Mục tiêu Tuần 1:**
  1. Loại bỏ triệt để các lỗi ngầm về quản lý bộ nhớ và đồng bộ hóa.
  2. Hiệu chuẩn lại bộ đếm thời gian RTO đưa tốc độ tải 1 MB từ 38s xuống dưới 5s.
  3. Hiện thực khối Doubly-Indirect Block trên đĩa ảo để lưu trữ và đọc file 1 MB thật.

---

## 2. Phân Công Chi Tiết & Giao Thoa Giữa Các Thành Viên

| Thành viên | Trách nhiệm chính trong Tuần 1 | Files tác động | Đầu ra (Deliverables) |
| :--- | :--- | :--- | :--- |
| **TV1** | - Hiện thực `mbufalloc_wait()` và thêm `wakeup(&mbuf_pool)` trong `mbuffree()`.<br>- Giải phóng `pending_m` khi bảng ARP đầy.<br>- Đưa `seq_end` vào `struct mbuf`.<br>- Hiệu chuẩn RTO timer về chuẩn 10 Hz (1 tick = 100ms). | `kernel/mbuf.c`<br>`kernel/arp.c`<br>`kernel/net.h`<br>`kernel/tcp_timer.c` | Tầng mạng không bị deadlock khi truyền tải dữ liệu liên tục; RTO timer hoạt động chính xác 200ms. |
| **TV2** | - Rà soát cơ chế khóa `pcb->lock`, chống race condition giữa ngắt mạng và timer.<br>- Bỏ ép kiểu dữ liệu `seq_end` trong `tcp_output` và `tcp_input`.<br>- Bổ sung xử lý cờ `TCP_FLAG_RST` để giải phóng socket khi client ngắt kết nối. | `kernel/tcp.c`<br>`kernel/tcp_input.c`<br>`kernel/tcp_output.c` | Tầng TCP an toàn luồng, không bị tê liệt khi client dùng curl bị ngắt đột ngột (`Ctrl+C`). |
| **TV3** | - Cải tạo cấu trúc Inode: Thêm Doubly-Indirect Block trong `bmap()`.<br>- Nâng kích thước đĩa `FSSIZE = 20000` và cập nhật `mkfs.c`.<br>- Tạo file nhị phân ngẫu nhiên `largefile.bin` 1 MB thật.<br>- Chuyển `sock_write` sang dùng `mbufalloc_wait()`.<br>- Viết lại `user/httpd.c` đọc file thật từ đĩa. | `kernel/fs.h`<br>`kernel/fs.c`<br>`mkfs/mkfs.c`<br>`kernel/param.h`<br>`kernel/sock.c`<br>`user/httpd.c` | Hệ thống tệp chứa được file 1 MB; Web server đọc dữ liệu thật từ đĩa qua `open()` và `read()`. |

---

## 3. Tiêu Chí Nghiệm Thu Cuối Tuần 1 (Definition of Done)

1. **Kiểm thử biên dịch:**
   ```bash
   make clean && make -j4
   ```
   Biên dịch thành công 100%, 0 warning, 0 error.

2. **Kiểm thử truyền tải 1 MB thật:**
   Khởi động QEMU với tùy chọn mạng ánh xạ port `20080`:
   ```bash
   curl -v http://127.0.0.1:20080/largefile.bin -o dl.bin
   ```
   - Thời gian tải file: $\le 5\text{ giây}$ khi cấu hình mất gói 10%.
   - Kiểm tra mã băm toàn vẹn dữ liệu:
     ```bash
     sha256sum dl.bin
     ```
     Trùng khớp 100% với mã SHA-256 của file `largefile.bin` gốc trên máy Host.

3. **Kiểm thử độ ổn định (Anti-Deadlock):**
   Chạy vòng lặp tải 10 lần liên tiếp: Web server không bị treo, kernel không panic.
