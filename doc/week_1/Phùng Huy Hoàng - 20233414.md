# TIẾN TRÌNH TUẦN 1 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai Giai Đoạn 2: Gia Cố Kernel Mạng, Khử Lỗi Deadlock, Cấu Hình Rớt Gói Động & Chuẩn Hóa RTO Timer

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 1 (Week 1)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

Tài liệu này mô tả chi tiết toàn bộ quá trình kiểm toán mã nguồn, phát hiện lỗi tiềm ẩn, thiết kế giải pháp kỹ thuật, triển khai mã nguồn và đo đạc kết quả thực nghiệm trong **Giai đoạn 2 (Tuần 1 – Tối ưu hóa toàn diện & Hoàn thiện nhiệm vụ Thành viên 1)** của dự án Minimal TCP & HTTP Server trên hệ điều hành **xv6-riscv** (64-bit).

---

## 1. Mục tiêu Giai đoạn 2

Giai đoạn 2 tập trung nâng cấp hệ thống mạng từ mức prototype sơ khai sang trạng thái **chịu tải bền bỉ, an toàn bộ nhớ và ổn định thời gian thực**:
1. **Khử triệt để nguy cơ Deadlock** khi cạn kiệt bộ đệm `mbuf` trong các phiên truyền dữ liệu lớn.
2. **Loại bỏ hoàn toàn hack ghi đè metadata** trong vùng đệm gói tin, bảo vệ tính toàn vẹn của headroom và payload.
3. **Chống rò rỉ bộ nhớ (Memory Leak)** trong quá trình phân giải địa chỉ ARP động.
4. **Cấu hình tỉ lệ rớt gói linh hoạt ở Runtime**: Hiện thực 2 system calls mới (`sys_set_loss_rate`, `sys_get_loss_rate`) và tiện ích người dùng `setloss` cho phép thay đổi tỉ lệ từ 0% đến 100%.
5. **Xử lý chuẩn cờ `TCP_FLAG_RST` theo RFC 793**: Đảm bảo giải phóng tài nguyên và đánh thức tiến trình ngay khi client ngắt kết nối đột ngột hoặc timeout.
6. **Chuẩn hóa chu kỳ RTO Timer về tần số 10 Hz**: Sửa sai lệch độ trễ retransmission từ 2.0s xuống 0.2s, tăng tốc độ truyền tải file 1 MB dưới mạng rớt gói 10% lên gần 10 lần (hoàn thành trong **4 giây**, thông lượng đạt **252.5 KB/s**, mã băm SHA-256 khớp 100%).

---

## 2. Chi Tiết Các Lỗi Phát Hiện, Nguyên Nhân Gốc Rễ & Giải Pháp Kỹ Thuật

### 2.1. Lỗi Deadlock khi cạn kiệt pool `mbuf` trong `sock_write()`
* **Triệu chứng:** Khi client tải file lớn, toàn bộ 128 `mbuf` bị giữ trong hàng đợi truyền lại `retransmit_q`. `sock_write()` gọi `mbufalloc()` trả về `0`, tiến trình rơi vào `sleep(pcb)` và bị treo vĩnh viễn.
* **Nguyên nhân gốc rễ:** Trong `kernel/mbuf.c`, hàm `mbuffree()` khi thu hồi `mbuf` về `free_list` chỉ thao tác trên danh sách liên kết mà **hoàn toàn không gọi `wakeup()`**. Do đó, không có cơ chế nào đánh thức các tiến trình đang chờ bộ đệm.
* **Giải pháp kỹ thuật:**
  1. Trong `kernel/mbuf.c`: Bổ sung `wakeup(&mbuf_pool)` trong `mbuffree()` mỗi khi một `mbuf` có `ref_cnt <= 0` được trả về `free_list`.
  2. Hiện thực hàm cấp phát có cơ chế chờ an toàn `mbufalloc_wait(headroom)`:
     ```c
     struct mbuf*
     mbufalloc_wait(unsigned int headroom)
     {
       struct mbuf *m;
       if (headroom > MBUF_SIZE) return 0;
       acquire(&mbuf_pool.lock);
       while ((m = mbuf_pool.free_list) == 0) {
         sleep_prepare(&mbuf_pool);
         release(&mbuf_pool.lock);
         sleep();
         acquire(&mbuf_pool.lock);
       }
       mbuf_pool.free_list = m->next;
       m->next = 0;
       m->ref_cnt = 1;
       m->seq_end = 0;
       m->head = m->buf + headroom;
       m->len = 0;
       release(&mbuf_pool.lock);
       return m;
     }
     ```
  3. Cập nhật `sock_write()` trong `kernel/sock.c` sử dụng `mbufalloc_wait()` thay cho vòng lặp sleep thủ công.

---

### 2.2. Nguy cơ hỏng vùng nhớ do hack ghi đè Metadata `seq_end` vào `clone->buf`
* **Triệu chứng:** Đoạn code Phase 1 cũ trong `kernel/tcp_output.c` lưu sequence kết thúc bằng cách ghi đè 4 byte đầu của vùng đệm dữ liệu:  
  `*(uint32 *)(clone->buf) = seq_end;`  
  và đọc lại trong `kernel/tcp_input.c` bằng:  
  `uint32 seg_end = *(uint32 *)((*pq)->buf);`
* **Nguy cơ:** `clone->buf` là mảng chứa dữ liệu thô. Khi gói tin được bọc các header qua `mbufpush()`, nếu `head` lùi sát về đầu mảng `buf`, thao tác ghi đè này sẽ phá hỏng header mạng hoặc làm sai lệch giá trị `seq_end` khi giải phóng ACK.
* **Giải pháp kỹ thuật:**
  - Bổ sung trường tường minh vào `struct mbuf` trong `kernel/net.h`:
     ```c
     struct mbuf {
       struct mbuf *next;
       char        *head;
       unsigned int len;
       int          ref_cnt;
       uint32       seq_end;        // metadata: sequence ket thuc cua segment
       char         buf[MBUF_SIZE];
     };
     ```
  - Cập nhật `kernel/tcp_output.c`: `clone->seq_end = seq_end;`.
  - Cập nhật `kernel/tcp_input.c`: `uint32 seg_end = (*pq)->seq_end;`.

---

### 2.3. Rò rỉ bộ nhớ `pending_m` trong phân giải địa chỉ ARP
* **Triệu chứng:** Khi có nhiều yêu cầu ARP dồn dập khiến bảng `arp_cache` đầy (16 entries đều `valid` và không tìm thấy slot trống), hàm `arp_resolve()` trả về `-1` nhưng bỏ rơi con trỏ gói tin `pending_m`.
* **Giải pháp kỹ thuật:** Bổ sung thu hồi `mbuffree(pending_m)` ngay trước khi thoát nếu bảng ARP không còn chỗ trống trong `kernel/arp.c`.

---

### 2.4. Điều khiển tỉ lệ rớt gói linh hoạt qua System Call (Configurable Loss-Injection)
* **Vấn đề cũ:** Tỉ lệ rớt gói 10% bị code cứng trong driver `kernel/e1000.c`. Mỗi khi muốn benchmark đối chứng (0% loss vs 10% loss vs 20% loss), lập trình viên buộc phải sửa code C và biên dịch lại kernel.
* **Giải pháp kỹ thuật:**
  1. Thêm biến `packet_loss_rate` và các hàm getter/setter có bảo vệ spinlock trong `kernel/e1000.c`:
     ```c
     static int packet_loss_rate = 10; // mặc định 10%
     int e1000_set_loss_rate(int rate) {
       if (rate < 0) rate = 0;
       if (rate > 100) rate = 100;
       acquire(&e1000_lock);
       packet_loss_rate = rate;
       release(&e1000_lock);
       return packet_loss_rate;
     }
     int e1000_get_loss_rate(void) {
       acquire(&e1000_lock);
       int r = packet_loss_rate;
       release(&e1000_lock);
       return r;
     }
     ```
  2. Bổ sung 2 system calls trong `kernel/sysnet.c`: `sys_set_loss_rate` (syscall 28) và `sys_get_loss_rate` (syscall 29).
  3. Đăng ký vào bảng phân phối syscall `kernel/syscall.c`, `kernel/syscall.h`, `user/user.h`, `user/usys.pl`.
  4. Viết công cụ người dùng `user/setloss.c`:
     ```c
     #include "kernel/types.h"
     #include "user/user.h"
     int main(int argc, char *argv[]) {
       if (argc < 2) {
         printf("Current E1000 packet loss rate: %d%%\n", get_loss_rate());
         printf("Usage: setloss <rate_percent (0-100)>\n");
         exit(0);
       }
       int rate = atoi(argv[1]);
       set_loss_rate(rate);
       printf("E1000 packet loss rate set to %d%%\n", rate);
       exit(0);
     }
     ```

---

### 2.5. Thiếu xử lý cờ `RST` khiến Web Server bị treo vĩnh viễn sau khi Client Timeout
* **Triệu chứng thực tế:**
  1. Khi chạy `curl --max-time 30 http://127.0.0.1:20080/file1mb -o downloaded.bin`, curl tải được **806 KB** thì chạm mốc timeout 30 giây.
  2. Curl trên Host Linux đóng kết nối và gửi cờ `RST` sang xv6.
  3. Khi người dùng chạy lệnh curl tiếp theo: `curl --max-time 60 ...`, curl đứng đợi đủ 60 giây và báo lỗi **`timed out after 60002 ms with 0 bytes received`**!
* **Nguyên nhân gốc rễ:**
  - File `kernel/tcp_input.c` hoàn toàn không có nhánh xử lý cờ `TCP_FLAG_RST`. Gói tin RST bị vứt bỏ trong im lặng.
  - Kết nối cũ trong kernel xv6 vẫn giữ trạng thái `TCP_ESTABLISHED` với 16 gói tin in-flight chưa được ACK.
  - Hàm `sock_write()` trong tiến trình `httpd` tiếp tục gọi `sleep(pcb)` chờ ACK của client cũ đã biến mất. Tiến trình không bao giờ thức dậy và không bao giờ quay lại vòng lặp `accept()` để phục vụ kết nối mới!
* **Giải pháp kỹ thuật:**
  1. Xử lý cờ `RST` ngay tại đầu hàm `tcp_input()` sau khi lấy khóa PCB:
     ```c
     if (flags & TCP_FLAG_RST) {
       if (pcb->state != TCP_LISTEN && pcb->state != TCP_CLOSED) {
         pcb->state = TCP_CLOSED;
         wakeup(pcb); // Đánh thức ngay sock_write đang ngủ!
         if (pcb->sock)
           sock_push_eof((struct socket *)pcb->sock);
       }
       release(&pcb->lock);
       mbuffree(m);
       return;
     }
     ```
  2. Trong `kernel/tcp.c`, tại nhánh retransmission bỏ cuộc khi vượt quá 12 lần timeout (`pcb->backoff > 12`):
     ```c
     if (pcb->backoff > 12) {
       pcb->state = TCP_CLOSED;
       wakeup(pcb); // Bổ sung wakeup để giải phóng tiến trình người dùng
       if (pcb->sock)
         sock_push_eof((struct socket *)pcb->sock);
       release(&pcb->lock);
       continue;
     }
     ```

---

### 2.6. Sai lệch tần số Timer (10 Hz vs 100 Hz) làm RTO chậm gấp 10 lần
* **Triệu chứng thực tế:** Khi chạy tải file 1 MB với 10% packet loss, tốc độ chỉ đạt ~27 KB/s và mất hơn 38 giây mới xong.
* **Nguyên nhân gốc rễ:**
  - Trong `kernel/start.c`, xv6 cấu hình chu kỳ ngắt đồng hồ:
    `w_stimecmp(r_time() + 1000000);`  
    Với xung nhịp phần cứng QEMU RISC-V Virt là 10 MHz ($10,000,000\text{ cycles/giây}$), $1,000,000\text{ cycles}$ tương ứng chính xác với **100 ms (tần số 10 Hz)**.
  - Tuy nhiên, trong code Phase 1 cũ:
    `#define TICKS_PER_SEC 100`  
    và hạn định RTO deadline được tính bằng:  
    `pcb->rto_deadline = ticks + (pcb->rto / 10);`
  - Vì chia cho 10 thay vì 100, giá trị RTO cơ sở 200 ms bị tính thành:  
    $$200 / 10 = 20\text{ ticks} = 20 \times 100\text{ ms} = 2.0\text{ GIÂY}!$$
  - Mỗi lần rớt gói và phải timeout retransmission, kernel dừng chờ tới **2.0 giây** rồi **4.0 giây** (Exponential Backoff), gây lãng phí băng thông nghiêm trọng.
* **Giải pháp kỹ thuật:**
  1. Trong `kernel/tcp_timer.c`:
     ```c
     #define TICKS_PER_SEC  10     // timer xv6 chay o 10hz (1 tick = 100ms)
     #define TICKS_TO_MS(t) ((t) * 100)
     #define MS_TO_TICKS(ms) (((ms) + 99) / 100)
     ```
  2. Chuẩn hóa tính toán deadline trong `kernel/tcp.c`, `kernel/tcp_input.c`, `kernel/tcp_output.c`:  
     `pcb->rto_deadline = ticks + (pcb->rto / 100 ? pcb->rto / 100 : 1);`  
     Khi RTO = 200 ms, deadline chuẩn là **2 ticks** (0.2 giây).

---

## 3. Kết Quả Đo Đạc Thực Nghiệm & Nghiệm Thu

### 3.1. Kiểm thử Syscall điều khiển rớt gói trong xv6 shell
```sh
$ setloss
Current E1000 packet loss rate: 10%
Usage: setloss <rate_percent (0-100)>

$ setloss 0
E1000 packet loss rate set to 0%

$ setloss 25
E1000 packet loss rate set to 25%

$ setloss 10
E1000 packet loss rate set to 10%
```

### 3.2. Kiểm thử tải file tĩnh `README`
Từ Linux Host terminal:
```bash
curl -v http://127.0.0.1:20080/README
```
* **Kết quả:** Phản hồi mã `HTTP/1.0 200 OK`, tải toàn bộ 4,283 bytes nội dung tài liệu sạch sẽ, đóng kết nối đúng chuẩn.

### 3.3. Kiểm thử tải file 1 MB dưới điều kiện rớt gói 10% & Đối chiếu SHA-256
Từ Linux Host terminal:
```bash
curl --max-time 60 http://127.0.0.1:20080/file1mb -o downloaded.bin
python3 -c 'open("expected.bin", "wb").write(bytes(((i * 37 + 13) & 0xff) for i in range(1048576)))'
sha256sum downloaded.bin expected.bin
```

* **Kết quả thực tế trên màn hình:**
```text
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 1.00M  100 1.00M    0     0   252.5k      0  0:00:04  0:00:04 --:--:--  258.7k

c4e1ab33a4d2f0d7be4e6be6c387096da51a28bad003f71309bb774f75139111  downloaded.bin
c4e1ab33a4d2f0d7be4e6be6c387096da51a28bad003f71309bb774f75139111  expected.bin
```

* **Đánh giá hiệu năng:**
  - **Dung lượng:** $1,048,576\text{ bytes}$ (đạt 100%).
  - **Thời gian truyền tải:** **Đúng 4 giây** (rút ngắn gần 10 lần so với trước khi chuẩn hóa RTO).
  - **Tốc độ truyền tải trung bình:** **252.5 KB/s**.
  - **Độ tin cậy:** Mã băm SHA-256 trùng khớp từng ký tự `c4e1ab33...`, chứng minh tầng TCP FSM, Fast Retransmit và Jacobson RTO Engine tự phục hồi trọn vẹn 100% dữ liệu dưới môi trường mạng rớt gói 10%.
  - **Độ bền bỉ:** Server `httpd` lập tức đón nhận và phục vụ các kết nối tiếp theo liên tục, không bị rò rỉ buffer hay treo socket.

---

## 4. Danh Sách Các File Mã Nguồn Đã Thay Đổi

| File | Thành phần | Nội dung thay đổi chính |
| :--- | :--- | :--- |
| `kernel/net.h` | Header cốt lõi | Thêm `seq_end` vào `struct mbuf`, thêm prototype `mbufalloc_wait()`, `e1000_set_loss_rate()`, `e1000_get_loss_rate()`. |
| `kernel/mbuf.c` | Quản lý bộ đệm gói | Thêm `wakeup(&mbuf_pool)` trong `mbuffree()`, hiện thực hàm cấp phát an toàn `mbufalloc_wait()`. |
| `kernel/e1000.c` | Driver card mạng | Hiện thực biến điều chỉnh rớt gói động `packet_loss_rate`, hàm `e1000_set_loss_rate()`, `e1000_get_loss_rate()`. |
| `kernel/arp.c` | Phân giải ARP | Thu hồi `mbuffree(pending_m)` khi bảng ARP Cache bị đầy. |
| `kernel/sock.c` | Socket layer | Chuyển `sock_write()` sang dùng `mbufalloc_wait()`. |
| `kernel/sysnet.c` | Syscalls mạng | Thêm syscall `sys_set_loss_rate` và `sys_get_loss_rate`. |
| `kernel/syscall.h` | Syscall headers | Định nghĩa `SYS_set_loss_rate (28)` và `SYS_get_loss_rate (29)`. |
| `kernel/syscall.c` | Bảng phân phối syscall | Đăng ký 2 syscalls mới vào mảng `syscalls[]`. |
| `kernel/tcp_input.c` | Xử lý gói TCP | Xử lý cờ `TCP_FLAG_RST` đánh thức tiến trình, chuẩn hóa RTO deadline về 10 Hz, đọc `(*pq)->seq_end`. |
| `kernel/tcp_output.c` | Phát gói TCP | Lưu `clone->seq_end = seq_end`, chuẩn hóa RTO deadline về 10 Hz. |
| `kernel/tcp.c` | Quản lý PCB | Chuẩn hóa RTO deadline về 10 Hz, thêm `wakeup(pcb)` và `sock_push_eof` khi `backoff > 12`. |
| `kernel/tcp_timer.c` | RTO Engine | Cấu hình tần số chuẩn `TICKS_PER_SEC = 10`, định nghĩa `MS_TO_TICKS`. |
| `user/user.h` | User-space header | Thêm prototype `set_loss_rate()`, `get_loss_rate()`. |
| `user/usys.pl` | Syscall stubs generator | Đăng ký entry `set_loss_rate` và `get_loss_rate`. |
| `user/setloss.c` | CLI Utility | Tiện ích dòng lệnh xem và điều chỉnh tỉ lệ rớt gói trên xv6 console. |
| `Makefile` | Build configuration | Thêm `$U/_setloss\` vào danh sách ứng dụng `UPROGS` tạo đĩa ảo `fs.img`. |
