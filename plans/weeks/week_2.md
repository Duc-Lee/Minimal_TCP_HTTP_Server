# KẾ HOẠCH PHỐI HỢP TUẦN 2 (WEEK 2 PLAN)
## Chủ đề: Hàng Đợi Gói Lệch Thứ Tự (Out-of-Order), RFC 793 RST, Syscall Setloss & State Tests

---

## 1. Trọng Tâm & Mục Tiêu Kỹ Thuật

* **Bối cảnh:** Khi mạng có tỉ lệ rớt gói 10%, các gói tin đến sau gói bị mất sẽ có sequence number lớn hơn `rcv_nxt`. Hiện tại kernel vứt bỏ các gói này, buộc phía đối tác phải truyền lại toàn bộ khiến băng thông bị lãng phí nghiêm trọng. Bên cạnh đó, việc cấu hình mất gói đang bị sửa cứng trong driver thay vì điều khiển linh hoạt.
* **Mục tiêu Tuần 2:**
  1. Xây dựng hàng đợi `ooo_queue` trong PCB để giữ lại các gói lệch thứ tự và ghép nối ngay khi gói thiếu đến.
  2. Hiện thực tính năng gửi gói `RST` phản hồi khi nhận packet tại cổng chưa mở theo RFC 793.
  3. Cài đặt các System Calls `set_loss_rate` / `get_loss_rate` và tiện ích CLI `setloss`.
  4. Viết bộ kiểm thử tự động máy trạng thái TCP (State Tests) bằng Python và Scapy.

---

## 2. Phân Công Chi Tiết & Giao Thoa Giữa Các Thành Viên

| Thành viên | Trách nhiệm chính trong Tuần 2 | Files tác động | Đầu ra (Deliverables) |
| :--- | :--- | :--- | :--- |
| **TV1** | - Chuyển drop cứng thành biến `packet_loss_rate`.<br>- Cài đặt 2 syscalls: `sys_set_loss_rate` và `sys_get_loss_rate`.<br>- Viết công cụ dòng lệnh `user/setloss.c`.<br>- Bổ sung cơ chế tự động xóa bản ghi ARP hết hạn (ARP Aging sau 60s). | `kernel/e1000.c`<br>`kernel/sysnet.c`<br>`kernel/syscall.h`<br>`kernel/syscall.c`<br>`user/user.h`<br>`user/usys.pl`<br>`user/setloss.c`<br>`kernel/arp.c` | Lệnh `setloss 10`, `setloss 0`, `setloss status` chạy trực tiếp từ shell xv6; bảng ARP tự làm sạch. |
| **TV2** | - Thêm danh sách liên kết `ooo_queue` vào `struct tcp_pcb`.<br>- Viết logic chèn gói lệch thứ tự theo thứ tự sequence và logic ghép nối lên socket khi có gói bù.<br>- Cài đặt hàm `tcp_send_rst()` gửi phản hồi RST khi nhận gói tại cổng đóng. | `kernel/net.h`<br>`kernel/tcp_input.c`<br>`kernel/tcp_output.c` | Không còn tình trạng vứt bỏ gói hợp lệ; Wireshark ghi nhận các segment out-of-order được ráp nối chính xác. |
| **TV3** | - Viết bộ kịch bản kiểm thử tự động `tests/state_tests.py` sử dụng Python và Scapy trên máy Host.<br>- Hoàn thiện xử lý lỗi socket: `sock_read` trả về EOF (0) khi nhận FIN/RST; `sock_write` trả về lỗi (-1) khi kết nối gãy. | `tests/state_tests.py`<br>`kernel/sock.c` | Bộ test tự động kiểm tra 11 trạng thái FSM chạy xanh 100%; socket đóng giải phóng bộ nhớ sạch sẽ. |

---

## 3. Các Ca Kiểm Thử Tự Động Trong Bộ Testbed Python (`tests/state_tests.py`)

1. **Test 1: Bắt tay 3 bước chuẩn (3-Way Handshake):**
   - Host gửi SYN -> xv6 trả lời SYN-ACK -> Host gửi ACK -> Kết nối đạt trạng thái `ESTABLISHED`.
2. **Test 2: Gửi gói đến cổng chưa mở (Port 81):**
   - Host gửi SYN tới cổng 81 -> Xác minh nhận lại gói tin có cờ `RST = 1` từ xv6 trong vòng 10ms.
3. **Test 3: Đóng kết nối một nửa (Half-Close):**
   - Host gửi cờ FIN -> Xác minh xv6 phản hồi ACK và chuyển sang trạng thái `CLOSE_WAIT`.
4. **Test 4: Gói tin đến lệch thứ tự (Out-of-Order Injection):**
   - Bắn các segment có Seq 2000 trước Seq 1000 -> Xác minh xv6 không drop mà lưu vào `ooo_queue`, sau đó ráp nối dữ liệu đầy đủ khi Seq 1000 tới.

---

## 4. Tiêu Chí Nghiệm Thu Cuối Tuần 2 (Definition of Done)

1. Gõ `setloss 10` trong xv6: Card mạng E1000 bắt đầu làm rơi 10% gói tin. Gõ `setloss 0`: Mạng hoạt động bình thường không mất gói.
2. Bộ kịch bản `python3 tests/state_tests.py` thực thi từ Host vượt qua 100% các bài test trạng thái FSM.
3. Tải file 1 MB khi có gói đến lệch thứ tự mượt mà, không xuất hiện hiện tượng nghẽn do retransmit lặp lại.
