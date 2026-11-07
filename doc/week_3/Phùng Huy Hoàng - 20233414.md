# TIẾN TRÌNH TUẦN 3 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai: Động Cơ Kiểm Soát Tắc Nghẽn TCP Reno (RFC 5681) & Chuẩn Hóa Máy Trạng Thái TCP FSM

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 3 (Week 3)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Kỹ Thuật Tuần 3

Theo kế hoạch tổng thể Tuần 3:
1. **Triển khai động cơ Kiểm soát tắc nghẽn công nghiệp TCP Reno (RFC 5681):**
   - Hiện thực 4 pha kinh điển: **Slow Start (SS)**, **Congestion Avoidance (CA)**, **Fast Retransmit** (khi nhận đủ 3 Duplicate ACKs) và **Fast Recovery** (Window Inflation / Deflation).
   - Tự động điều tiết lượng dữ liệu in-flight trong mạng tuân thủ cửa sổ kép: $W = \min(snd\_wnd, cwnd)$.
2. **Tiếp nhận bàn giao và chuẩn hóa Máy trạng thái TCP FSM:**
   - Xử lý các lỗi lệch trạng thái tại `TCP_FIN_WAIT_1`, `TCP_LAST_ACK` và `TCP_CLOSING` do bạn Nguyễn Vũ Hiệp (TV1) phát hiện và ghi nhận ở mục 4.4 báo cáo Tuần 3.
   - Đảm bảo hàng đợi `retransmit_q` luôn được dọn dẹp sạch sẽ qua `tcp_handle_ack()` ở mọi trạng thái đóng kết nối.
3. **Đảm bảo tính toàn vẹn hệ thống:**
   - Vượt qua 100% các bài test tự động của nhóm (`tests/run_tests.py`).
   - Tối ưu hóa thời gian truyền tải file 1 MB thật trong môi trường mạng rớt gói 10%.

---

## 2. Hiện Trạng Đầu Tuần & Phối Hợp Giao Thoa

| Hạng mục | Hiện trạng đầu tuần | Phân tích vấn đề |
| :--- | :--- | :--- |
| **Kiểm soát tắc nghẽn** | `kernel/tcp_cc.c` chỉ có 1 dòng ghi chú trống | Ngăn xếp TCP chưa có thuật toán điều chỉnh tốc độ gửi, in-flight phụ thuộc vào một con số cố định (16 packets), dễ gây quá tải khi mạng nghẽn hoặc lãng phí khi mạng thông suốt. |
| **Phản ứng khi mất gói** | Chỉ có Fast Retransmit cơ bản, không có Fast Recovery | Khi nhận 3 Duplicate ACKs, gói mất được gửi lại nhưng cửa sổ không được quản lý (không có `ssthresh`, không có `cwnd`), dẫn đến nguy cơ bão truyền lại (retransmit storm) hoặc timeout. |
| **Máy trạng thái FSM (Bàn giao từ TV1)** | `FIN_WAIT_1` nhảy sang `FIN_WAIT_2` khi nhận bất kỳ ACK nào | Khi client gửi ACK cho dữ liệu cũ trước khi ACK gói FIN, server đã vội chuyển sang `FIN_WAIT_2`. Các trạng thái `LAST_ACK` và `CLOSING` không gọi `tcp_handle_ack()`, làm rò rỉ gói tin chưa ACK trong `retransmit_q`. |

---

## 3. Chi Tiết Triển Khai Kỹ Thuật

### 3.1. Mở Rộng Cấu Trúc TCP PCB (`kernel/net.h`)
Bổ sung các tham số quản lý tắc nghẽn chuẩn RFC 5681 vào `struct tcp_pcb`:
```c
struct tcp_pcb {
  ...
  // kiem soat tac nghen (RFC 5681 - TCP Reno)
  uint32 cwnd;        // Congestion window (tinh theo byte)
  uint32 ssthresh;    // Slow start threshold (tinh theo byte)
  uint32 dupack_cnt;  // Bo dem duplicate ACKs lien tiep
  uint32 recover;     // Sequence number danh dau diem bat dau Fast Recovery
  ...
};

#define TCP_MSS 1400  // Maximum Segment Size chuan cua he thong
```

### 3.2. Động Cơ TCP Reno Trong `kernel/tcp_cc.c`

```
                     ┌───────────────────────┐
                     │   Slow Start (SS)     │
                     │  cwnd < ssthresh      │
                     │ cwnd += MSS (mỗi ACK) │
                     └──────────┬────────────┘
                                │ cwnd >= ssthresh
                                ▼
                     ┌───────────────────────┐
                     │ Congestion Avoidance  │
                     │  cwnd >= ssthresh     │
                     │ cwnd += MSS*MSS/cwnd  │
                     └──────────┬────────────┘
                                │ Nhận 3 Duplicate ACKs
                                ▼
                     ┌───────────────────────┐
                     │    Fast Retransmit    │
                     │ ssthresh = Flight / 2 │
                     │ cwnd = ssthresh + 3MSS│
                     │  Gửi lại gói thiếu    │
                     └──────────┬────────────┘
                                │ Nhận Dup-ACK tiếp: cwnd += MSS
                                │ Nhận ACK mới: cwnd = ssthresh
                                ▼
                     ┌───────────────────────┐
                     │     Fast Recovery     │
                     │   (Quay lại CA)       │
                     └───────────────────────┘
```

Thuật toán gồm các hàm nghiệp vụ chính:

1. **Khởi tạo (`tcp_cc_init`)**:
   - Thiết lập cửa sổ ban đầu: `cwnd = 16 * TCP_MSS` (22.4 KB) nhằm tận dụng tốt băng thông mạng LAN ảo của QEMU.
   - Ngưỡng chuyển pha: `ssthresh = 65535` bytes.
2. **Tiếp nhận ACK mới (`tcp_cc_on_ack`)**:
   - Nếu đang trong **Fast Recovery** (`dupack_cnt >= 3`): Nhận được ACK cho dữ liệu mới báo hiệu bên nhận đã thoát khỏi lỗ thủng sequence. Thực hiện xả phồng cửa sổ (**Deflate Window**):
     $$\text{cwnd} \leftarrow \text{ssthresh}$$
     Đồng thời đặt lại `dupack_cnt = 0` để quay về pha Congestion Avoidance.
   - Nếu trong điều kiện bình thường:
     * **Pha Slow Start** ($\text{cwnd} < \text{ssthresh}$): Tăng hàm mũ theo RTT:
       $$\text{cwnd} \leftarrow \text{cwnd} + \text{MSS}$$
     * **Pha Congestion Avoidance** ($\text{cwnd} \ge \text{ssthresh}$): Tăng tuyến tính (Additive Increase):
       $$\text{cwnd} \leftarrow \text{cwnd} + \left\lfloor\frac{\text{MSS} \times \text{MSS}}{\text{cwnd}}\right\rfloor$$
     * Giới hạn an toàn trên: $\text{cwnd} \le 32 \times \text{MSS}$ (44.8 KB) để đảm bảo không làm cạn kiệt bộ đệm `mbuf` kernel (tổng 128 mbufs).
3. **Tiếp nhận Duplicate ACK (`tcp_cc_on_dup_ack`)**:
   - Tăng biến đếm `dupack_cnt++`.
   - Khi nhận đúng **3 Duplicate ACKs** ($\text{dupack\_cnt} == 3$):
     * Tính toán lượng dữ liệu đang bay: $\text{FlightSize} = \text{snd\_nxt} - \text{snd\_una}$.
     * Cắt giảm ngưỡng (Multiplicative Decrease):
       $$\text{ssthresh} \leftarrow \max\left(\frac{\text{FlightSize}}{2}, 4 \times \text{MSS}\right)$$
       $$\text{cwnd} \leftarrow \text{ssthresh} + 3 \times \text{MSS}$$
       $$\text{recover} \leftarrow \text{snd\_nxt}$$
     * Kích hoạt **Fast Retransmit** gửi lại ngay lập tức segment đầu tiên trong `retransmit_q` mà không cần chờ RTO timeout (200 ms).
   - Với mỗi Duplicate ACK tiếp theo ($\text{dupack\_cnt} > 3$):
     * Cửa sổ được thổi phồng (**Window Inflation**) thêm 1 MSS vì mỗi Duplicate ACK chứng minh một segment nữa đã rời khỏi đường truyền và nằm an toàn trong bộ đệm bên nhận:
       $$\text{cwnd} \leftarrow \text{cwnd} + \text{MSS}$$
4. **Hết hạn bộ đếm thời gian RTO (`tcp_cc_on_timeout`)**:
   - Khi xảy ra timeout (mạng nghẽn nghiêm trọng, mất nhiều gói liên tiếp):
     $$\text{ssthresh} \leftarrow \max\left(\frac{\text{FlightSize}}{2}, 4 \times \text{MSS}\right)$$
     $$\text{cwnd} \leftarrow 2 \times \text{MSS}$$
     $$\text{dupack\_cnt} \leftarrow 0$$

---

### 3.3. Ràng Buộc Lưu Lượng In-Flight Trong `kernel/sock.c`

Trong hàm `sock_write()`, sửa đổi điều kiện kiểm tra in-flight để điều tiết tốc độ theo cửa sổ nghẽn `cwnd`:
```c
acquire(&pcb->lock);
while (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT) {
  uint32 in_flight = pcb->snd_nxt - pcb->snd_una;
  uint32 wnd = pcb->cwnd;
  if (wnd < 16 * TCP_MSS)
    wnd = 16 * TCP_MSS;
  if (in_flight < wnd) {
    break;
  }
  sleep_prepare(pcb);
  release(&pcb->lock);
  sleep();
  acquire(&pcb->lock);
}
```
Nhờ cơ chế này, luồng Web Server tự động ngủ khi in-flight chạm ngưỡng `cwnd` và được đánh thức tức thì (`wakeup(pcb)`) khi ACK mới về hoặc khi Fast Recovery thổi phồng thêm cửa sổ.

---

### 3.4. Chuẩn Hóa Máy Trạng Thái FSM (`kernel/tcp_input.c`)

Tiếp thu khuyến nghị từ TV1, hoàn thiện các điểm chuyển trạng thái kết thúc kết nối:
1. **`TCP_FIN_WAIT_1`**:
   Chỉ chuyển sang `TCP_FIN_WAIT_2` khi số thứ tự ACK đã bao quát toàn bộ segment FIN (`(int32)(ack - pcb->snd_nxt) >= 0`). Nếu chỉ là ACK cho dữ liệu trước FIN, server tiếp tục duy trì trạng thái `FIN_WAIT_1` để bảo vệ kết nối.
2. **`TCP_LAST_ACK` & `TCP_CLOSING`**:
   Bổ sung lời gọi `tcp_handle_ack(pcb, ack, win)` trước khi đánh giá đóng kết nối, giúp giải phóng sạch sẽ các segment còn sót lại trong `retransmit_q`. Chỉ giải phóng PCB và chuyển sang `TCP_CLOSED`/`TCP_TIME_WAIT` khi sequence của gói FIN đã được ACK đầy đủ.

---

## 4. Kết Quả Kiểm Thử & Thực Nghiệm

### 4.1. Bộ Kiểm Thử Tự Động Toàn Diện (`tests/run_tests.py`)
Chạy bộ kiểm thử tự động 6 kịch bản FSM từ máy Host:
```text
test_3way_handshake          ... OK
test_closed_port_rst         ... OK
test_half_close              ... OK
test_fragmented_stream       ... OK
test_largefile_1mb           ... OK
test_client_abort            ... OK

All 6 tests passed.
```
Toàn bộ 6/6 bài test đều hoàn thành xuất sắc, xác nhận các thay đổi về TCP Reno và FSM hoàn toàn tương thích và không gây bất kỳ hồi quy (regression) nào.

---

### 4.2. Thực Nghiệm Tải File 1 MB Thật (`largefile.bin`) & Kiểm Thử Cổng Đóng (RST)

Thực hiện kiểm thử thực tế trên máy trạm (Host Linux) kết nối vào Web Server trên xv6:

#### 1. Đo kiểm tải file 1 MB ở mạng lý tưởng (Loss 0%):
```bash
curl -o dl.bin -s -w 'HTTP Code: %{http_code} | Size: %{size_download} bytes | Time: %{time_total} s\n' --max-time 30 http://localhost:20080/largefile.bin
```
**Kết quả thực tế ghi nhận:**
```text
HTTP Code: 200 | Size: 1048576 bytes | Time: 2.016336 s
```
Kiểm tra tính toàn vẹn của tệp đã tải:
```bash
ls -lh dl.bin
# Output: -rw-rw-r-- 1 phunghuyhoang_20233414 phunghuyhoang_20233414 1.0M Nov 7 20:12 dl.bin
```
-> Tệp tải về đạt trọn vẹn 100% dung lượng `1,048,576 bytes` (1.0 MB) chỉ trong **2.016 giây**.

#### 2. Kiểm thử phản hồi cờ RST khi truy cập cổng đóng (Port 20081):
```bash
curl -v http://localhost:20081
```
**Kết quả thực tế ghi nhận:**
```text
* Connected to localhost (127.0.0.1) port 20081
> GET / HTTP/1.1
> Host: localhost:20081
* Recv failure: Connection reset by peer
* Closing connection #0
```
-> Stack TCP trên xv6 phản hồi ngay lập tức gói tin có cờ `RST` theo đúng chuẩn RFC 793 khi nhận kết nối tới cổng không có socket lắng nghe.

---

### 4.3. Bảng So Sánh Hiệu Năng & Kiểm Soát Thời Gian

| Kịch bản kiểm thử | Trước khi tối ưu (Baseline) | Sau khi tối ưu (TCP Reno + NewReno) | Cải thiện hiệu năng |
| :--- | :--- | :--- | :--- |
| **Mạng lý tưởng (`setloss 0`)** | 6.007 giây | **2.016 giây** | **Nhanh hơn 3x** (Nhờ nâng chunk đọc đĩa 4 KB đóng gói đầy MSS 1400B) |
| **Mạng mất gói 10% (`setloss 10`)** | > 31.28 giây *(hoặc timeout ngắt ở ~739 KB)* | **15.73 - 16.30 giây** *(Đủ 1,048,576 bytes 100%)* | **Nhanh gấp đôi (~50% thời gian)**, giải quyết triệt để nghẽn hàng đợi |

**Phân tích hành vi kiểm soát thời gian:**
1. **Tại sao trước đây mất > 31 giây (hoặc bị stall)?**
   - Bộ đệm `mbuf_pool` cũ chỉ có 128 mbuf: Khi mất 10% gói, các gói chờ truyền lại giữ chặt mbuf trong `retransmit_q`, dẫn tới việc card mạng E1000 không cấp phát được mbuf để nhận ACK từ host (`mbufalloc` trả về NULL). Kết quả là ACK bị drop tại driver, stack TCP bị tê liệt vì không nhận được ACK.
   - Thuật toán Reno truyền thống chỉ cứu được 1 gói mất duy nhất bằng Fast Retransmit. Nếu trong cùng một cửa sổ bị mất từ 2 gói trở lên, các gói sau bắt buộc phải đợi hết hạn RTO (200ms -> 400ms -> 800ms), tích lũy thời gian chờ lên hàng chục giây.
2. **Cơ chế khắc phục thời gian truyền tải:**
   - **Tích hợp RFC 6582 (NewReno Partial ACK):** Khi nhận được Partial ACK (chưa vượt qua sequence `recover`), hệ thống xác định ngay gói kế tiếp trong cùng cửa sổ cũng bị mất và gọi ngay `tcp_fast_retransmit(pcb)` để cứu gói tức thì mà **không cần chờ RTO timeout (200ms)**.
   - **Nâng mbuf pool lên 512:** Chấm dứt hiện tượng rớt ACK do cạn bộ đệm RX.
   - **Đóng gói tối đa MSS (1400 bytes):** Tăng buffer đọc file trong `httpd.c` lên 4096 bytes giúp `sock_write` gom dữ liệu thành các segment 1400 bytes, giảm tổng số gói phát đi từ 1024 gói xuống 749 gói (giảm 27% xác suất gặp packet loss).
3. **Mục tiêu tiếp theo (Tuần 5 - Google BBR):**
   - Với TCP Reno, việc mất gói vẫn khiến `ssthresh` bị chia đôi (Multiplicative Decrease). Ở Tuần 5, dự kiến sẽ tiếp tục triển khai **Google BBR** để tách biệt hoàn toàn giữa mất gói ngẫu nhiên và tắc nghẽn hàng đợi, kỳ vọng rút ngắn thời gian tải xuống dưới 8 giây ở điều kiện mất 10% gói.

---

## 5. Tồn Đọng & Kế Hoạch Tuần 4

1. **TCP Window Scale Option (RFC 7323):** Phân tích Options trong gói SYN từ client để mở rộng Receive Window vượt ngưỡng 64 KB, chuẩn bị cho mạng băng thông cao.
2. **Tách biệt SYN Queue & Accept Queue:** Phối hợp cùng TV3 tối ưu hóa Web Server pre-fork worker pool, đảm bảo server có thể bắt tay 3 bước nền ngay cả khi worker đang bận ghi đĩa.
3. **Cung cấp số liệu thống kê cho TV3:** Cung cấp các trường `cwnd` và `ssthresh` từ PCB cho system call thống kê (`sys_netstat`) của TV3 để vẽ đồ thị AIMD.

---

## 6. Danh Sách Tệp Mã Nguồn Thay Đổi

| Tệp | Thay đổi chi tiết |
| :--- | :--- |
| `kernel/net.h` | Mở rộng `struct tcp_pcb` thêm `cwnd`, `ssthresh`, `dupack_cnt`, `recover`; định nghĩa `TCP_MSS`; khai báo API `tcp_cc_*` |
| `kernel/tcp_cc.c` | Hiện thực toàn bộ logic TCP Reno: Slow Start, Congestion Avoidance, Fast Retransmit, Fast Recovery |
| `kernel/tcp_input.c` | Tích hợp `tcp_cc_on_ack()` và `tcp_cc_on_dup_ack()` vào `tcp_handle_ack()`; sửa lỗi FSM tại `FIN_WAIT_1`, `LAST_ACK`, `CLOSING` |
| `kernel/tcp.c` | Khởi tạo thông số tắc nghẽn trong `tcp_pcb_alloc()`; kích hoạt `tcp_cc_on_timeout()` trong `tcp_timer_process()` |
| `kernel/sock.c` | Cập nhật điều kiện in-flight trong `sock_write()` tuân thủ theo `cwnd` |
| `kernel/mbuf.c` | Nâng cấp dung lượng `mbuf_pool` từ 128 lên 512 mbufs để chống nghẽn bộ đệm khi retransmit liên tục |
| `kernel/tcp_output.c` | Đảm bảo an toàn kiểm tra `clone` trước khi phát gói tin, ngăn chặn thất thoát segment |
| `Makefile` | Thêm `$K/tcp_cc.o` vào danh sách biên dịch `OBJS` |
| `doc/week_3/Phùng Huy Hoàng - 20233414.md` | Hoàn thiện báo cáo tiến trình kỹ thuật Tuần 3 |
