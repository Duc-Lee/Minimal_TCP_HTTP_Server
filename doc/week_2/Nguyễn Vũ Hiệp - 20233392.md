# TIẾN TRÌNH TUẦN 2 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai: Bộ Giả Lập Mất Gói Động (Configurable Loss Simulator), Công Cụ `setloss` & Cơ Chế ARP Aging

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 2 (Week 2)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục tiêu

1. Cho phép điều khiển tỉ lệ rơi gói (0–100%) của card E1000 ngay khi hệ thống đang chạy, không cần biên dịch lại kernel.
2. Cung cấp công cụ CLI `setloss` (`setloss <rate>`, `setloss status`).
3. Bổ sung ARP Cache Aging: tự động xóa bản ghi ARP không hoạt động quá 60 giây.

---

## 2. Hiện trạng đầu tuần

Kiểm tra mã nguồn cho thấy các phần sau đã có sẵn và build được:

| Hạng mục | Vị trí |
| :--- | :--- |
| Biến `packet_loss_rate`, `e1000_set_loss_rate()`, `e1000_get_loss_rate()` | `kernel/e1000.c` |
| `SYS_set_loss_rate` (28), `SYS_get_loss_rate` (29) | `kernel/syscall.h`, `kernel/syscall.c` |
| `sys_set_loss_rate()`, `sys_get_loss_rate()` | `kernel/sysnet.c` |
| Wrapper userspace | `user/user.h`, `user/usys.pl` |
| `user/setloss.c` và mục `_setloss` trong `UPROGS` | `user/setloss.c`, `Makefile` |

Các phần còn thiếu hoặc sai: `setloss status` (bị `atoi` hiểu thành 0 nên tắt loss), loss mặc định 10% ngay khi boot, ARP Aging chưa có, và một lỗi trong ARP pending queue.

---

## 3. Chi Tiết Triển Khai

### 3.1. Sửa công cụ `user/setloss.c`
* **Vấn đề:** `setloss status` gọi `atoi("status") = 0` rồi đặt loss về 0%. Chuỗi không phải số như `abc` cũng bị hiểu thành 0% mà không báo lỗi.
* **Giải pháp:**
  - Kiểm tra `strcmp(argv[1], "status") == 0` trước `atoi`, chỉ in giá trị `get_loss_rate()`.
  - Thêm `is_number()` để từ chối tham số không phải số (kể cả số âm).
  - Giữ kiểm tra khoảng 0–100.

### 3.2. Đổi loss mặc định về 0% (`kernel/e1000.c`)
* **Vấn đề:** `packet_loss_rate = 10` khiến mạng mất gói ngay từ lúc boot, trái với tiêu chí nghiệm thu (`setloss 0` là mạng bình thường).
* **Giải pháp:** đặt mặc định `packet_loss_rate = 0`. Mạng chỉ mất gói khi người dùng chạy `setloss N`.

### 3.3. ARP Cache Aging (`kernel/arp.c`, `kernel/trap.c`, `kernel/net.h`)
* Thêm trường `uint last_seen_tick` vào `struct arp_entry`.
* `last_seen_tick` được cập nhật khi học hoặc cập nhật entry trong `arp_input()`, và khi entry được dùng (cache hit) trong `arp_resolve()`. Entry đang được dùng sẽ không bị xóa.
* Thêm `arp_age_tick()`, gọi từ `clockintr()` trong `kernel/trap.c` ngay sau `tcp_timer_tick()`. Hàm chỉ quét thực sự mỗi 1 giây (10 tick):

| Hằng số | Giá trị | Ý nghĩa |
| :--- | :--- | :--- |
| `ARP_AGE_TICKS` | 600 tick (60 giây) | Entry hợp lệ không dùng quá ngưỡng này bị xóa |
| `ARP_PENDING_TICKS` | 50 tick (5 giây) | Entry chờ ARP reply quá lâu bị bỏ, giải phóng gói đang chờ |
| `ARP_SWEEP_TICKS` | 10 tick (1 giây) | Chu kỳ quét bảng |

* Hàm trợ giúp `arp_drop_pending()` gọi `mbuffree()` cho từng mbuf trong `pending_queue` để không rò rỉ bộ nhớ.
* Xv6 chạy timer ở 10 Hz (1 tick = 100 ms), nên 60 giây tương ứng 600 tick.

### 3.4. Sửa lỗi ARP pending queue (phát hiện trong lúc kiểm tra)
* **Triệu chứng:**
  - `arp_resolve()` tạo slot `valid = 0` rồi gắn gói vào `pending_queue`.
  - `arp_input()` chỉ khớp entry có `valid && ip == sip`, nên khi reply đến sẽ tạo slot mới.
  - Hậu quả: các gói chờ không được gửi đi và mbuf bị rò rỉ.
  - Ngoài ra, slot "trống" chỉ kiểm tra `!valid`, nên một entry mới có thể ghi đè slot đang giữ gói chờ.
* **Giải pháp:**
  - `arp_input()` khớp cả entry đang chờ (`valid = 0` nhưng còn `pending_queue`), đặt `valid = 1` và đẩy gói đi.
  - Slot trống phải thỏa `!valid && !pending_queue`.
  - `arp_resolve()` dùng lại slot đang chờ của cùng IP thay vì tạo entry trùng.

---

## 4. Kết Quả Kiểm Thử

Môi trường: QEMU `virt`, E1000 (user-mode network, `hostfwd tcp::20080-:80`), Host dùng `curl`.

### 4.1. Lệnh `setloss`

```
$ setloss status
Current E1000 packet loss rate: 0%
$ setloss 10
E1000 packet loss rate set to 10%
$ setloss status
Current E1000 packet loss rate: 10%
$ setloss abc
Error: invalid argument 'abc'
Usage: setloss <rate_percent (0-100)> | status
$ setloss 150
Error: loss rate must be between 0 and 100
$ setloss 0
E1000 packet loss rate set to 0%
$ setloss status
Current E1000 packet loss rate: 0%
```

### 4.2. Tải file `largefile.bin` (1 MB) qua `httpd`

| Loss rate | HTTP | Kích thước tải | Thời gian | MD5 so với file gốc |
| :---: | :---: | :---: | :---: | :---: |
| 0% | 200 | 1,048,576 bytes | 0.59 s | Trùng khớp |
| 10% | 200 | 1,048,576 bytes | 4.10 s | Trùng khớp |

Lần đo đầu tiên (một lần tải mỗi mức) cho kết quả đẹp ở 10% loss. Các phép đo lặp ở mục 4.4 cho thấy kết quả này không ổn định.

### 4.3. ARP Aging
Kiểm chứng bằng bản build gỡ lỗi tạm thời (giảm `ARP_AGE_TICKS` xuống 10 giây và thêm log). Sau khi kiểm chứng đã khôi phục mã gốc và kiểm tra không còn dấu vết debug.

| Bước | Quan sát |
| :--- | :--- |
| Tải lần 1 | Tạo entry ARP cho 10.0.2.2 |
| Nghỉ 15 giây | Log `aged out ip=a000202`: entry bị xóa đúng khi hết hạn |
| Tải lần 2 | Kernel gửi ARP request, nhận reply, log `flush pending ip=a000202`; tải 1 MB thành công, MD5 trùng |
| Nghỉ 15 giây, `setloss 10`, tải lần 3 | Lặp lại aging và flush; tải thành công, MD5 trùng |

Kết luận: ARP Aging hoạt động, và cơ chế đẩy `pending_queue` sau khi phân giải lại đã chạy đúng (lỗi ở mục 3.4 đã được sửa).

Chưa kiểm chứng riêng nhánh "pending timeout" (entry chờ reply quá 5 giây rồi bị bỏ), vì cần một IP đích không phản hồi ARP. Nhánh này chỉ được kiểm tra bằng đọc mã và biên dịch.

### 4.4. Kiểm thử hồi quy và độ ổn định khi có loss

**Loss 0%:** tải lặp 10 lần liên tiếp, cả 10 lần thành công (0.43–0.69 s), MD5 trùng. Không có panic hay deadlock.

**Các mức loss khác nhau (một lần tải mỗi mức, timeout 90 s):**

| Loss | Kết quả |
| :---: | :--- |
| 0% | 0.69 s, thành công |
| 10% | 4.15 s, thành công |
| 20% | 27.7 s, thành công |
| 30% | Thất bại (timeout 90 s, không nhận được dữ liệu) |

**Tải lặp 8 lần ở 10% loss (timeout 40 s), so sánh bản gốc `HEAD` với bản đã sửa:**

| | Bản gốc (HEAD) | Bản đã sửa |
| :--- | :---: | :---: |
| Thành công | 4/8 | 4/8 |
| Thất bại | 4/8 | 4/8 |
| Thời gian các lần thành công | 4.19, 4.70, 2.33, 1.97 s | 4.27, 4.70, 2.33, 1.98 s |

Cả hai bản cho kết quả gần như giống hệt nhau. Bộ sinh số giả ngẫu nhiên dùng seed cố định nên kết quả tái hiện được. **Các thay đổi của tuần này không gây hồi quy**, và lỗi dưới đây đã có từ trước.

**Hiện tượng thất bại (có sẵn từ trước):**
* Một số lần tải bị treo đến hết timeout. Có lần không nhận được phản hồi nào (`http=000`), có lần nhận thiếu phần cuối (1,047,552, 1,030,144 hoặc 1,043,456 bytes, đều là bội số của 1024).
* Sau các lần treo, hệ thống vẫn phục vụ tiếp được các kết nối sau, không panic hay deadlock.
* Đây là lỗi độ tin cậy của TCP khi có mất gói (truyền lại hoặc xử lý đóng kết nối), chưa xác định nguyên nhân gốc. Một giả thuyết cần kiểm tra: dữ liệu chưa được ACK bị bỏ khi `close()`, hoặc gói đóng kết nối bị mất mà không được truyền lại.

---

## 5. Tồn Đọng & Việc Tiếp Theo

* **KPI "tải 1 MB với 10% loss ≤ 5 giây" chưa đạt ổn định.** Khi đo lặp, 4/8 lần tải thất bại (mục 4.4), cả ở bản gốc lẫn bản đã sửa. Cần điều tra nguyên nhân gốc trong tầng TCP (truyền lại, xử lý `close()`/FIN). Phần này liên quan tới TV2 (`ooo_queue`, `tcp_input.c`/`tcp_output.c`) và TV3 (`sock.c`), cũng như `tcp_timer.c` của TV1 (Karn, giới hạn truyền lại ở Tuần 3).
* Chưa kiểm chứng riêng nhánh "pending timeout" của ARP (cần một IP đích không trả lời ARP).
* Loss hiện chỉ áp dụng ở chiều TX (`e1000_transmit`), gói ARP được loại trừ để không mất phân giải MAC. Nếu bộ test của TV3 cần loss ở chiều RX thì phải bổ sung.
* Phối hợp với TV2 để chạy lại kịch bản tải 1 MB khi `ooo_queue` hoàn tất, theo tiêu chí nghiệm thu chung của tuần.

---

## 6. Danh Sách Tệp Thay Đổi

| Tệp | Thay đổi |
| :--- | :--- |
| `user/setloss.c` | Thêm `status`, `is_number()`, kiểm tra đầu vào |
| `kernel/e1000.c` | Loss mặc định 0% |
| `kernel/arp.c` | `last_seen_tick`, `arp_age_tick()`, `arp_drop_pending()`, sửa pending queue |
| `kernel/net.h` | Khai báo `arp_age_tick()` |
| `kernel/trap.c` | Gọi `arp_age_tick()` trong `clockintr()` |
