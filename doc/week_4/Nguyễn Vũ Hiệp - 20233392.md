# TIẾN TRÌNH TUẦN 4 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai: Chống Nghẽn Hàng Đợi NIC E1000, Tối Ưu Hóa Cấp Phát Mbuf Đa Kết Nối & Cảnh Báo Low Watermark

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 4 (Week 4)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục tiêu

1. Nâng kích thước và tối ưu cơ chế quản lý Ring Buffer của card mạng E1000 (`kernel/e1000.c`), thu hồi sớm các Descriptor TX đã truyền xong (`DD = 1`) thay vì giữ `mbuf` cho đến khi vòng lặp quay lại đúng slot cũ.
2. Hạn chế rơi gói tại ngắt nhận (`e1000_recv`) khi nhiều kết nối đồng thời làm cạn tạm thời `mbuf_pool` hoặc khi CPU bận xử lý tiến trình.
3. Phân tích định lượng ngưỡng chịu tải của `mbuf_pool` dưới mô hình đa kết nối (Worker Pool), nâng kích thước pool phù hợp với dung lượng RAM hệ thống.
4. Bổ sung bộ đếm `mbuf_free_count`, theo dõi mức rảnh thấp nhất (`min_free`), phát cảnh báo **Low Watermark** khi lượng `mbuf` rảnh tụt dưới 10% kèm cơ chế trễ (hysteresis) chống ngập console, và kiểm chứng không rò rỉ `mbuf` sau tải đồng thời.

---

## 2. Hiện trạng đầu tuần

Kiểm tra mã nguồn đầu Tuần 4 cho thấy các điểm nghẽn sau khi hướng tới tải đa kết nối:

| Hạng mục | Hiện trạng đầu tuần | Hệ quả khi chạy đa kết nối | Vị trí |
| :--- | :--- | :--- | :--- |
| Kích thước `TX_RING_SIZE` / `RX_RING_SIZE` | Cố định 16 descriptor mỗi chiều | Burst từ nhiều worker dễ làm đầy ring trước khi kịp xử lý ngắt | `kernel/e1000.c` |
| Giải phóng `mbuf` trên TX ring | Chỉ giải phóng `tx_mbufs[tdt]` khi `tdt` quay vòng lại đúng slot đó | Tối đa 16 `mbuf` đã gửi xong vẫn bị ngâm trên TX ring, cộng với 16 `mbuf` giữ thường trực ở RX ring chiếm tới 32/128 (25%) toàn bộ pool | `kernel/e1000.c` |
| Ngắt E1000 (`E1000_IMS`) | Chỉ bật `E1000_ICR_RXT0`, đọc `ICR` 1 lần mỗi ngắt | Không có ngắt báo hàng đợi TX rỗng (`TXQE`), không cảnh báo sớm khi RX descriptor gần cạn (`RXDMT0`) hoặc tràn (`RXO`) | `kernel/e1000.c` |
| Xử lý thiếu `mbuf` ở `e1000_recv()` | Khi `mbufalloc(0)` trả về `0`, bỏ gói âm thầm và không thử thu hồi `mbuf` đang nằm trên TX ring | Mất gói ACK/data không có dấu vết thống kê | `kernel/e1000.c` |
| Kích thước `mbuf_pool` | `MBUF_POOL_SIZE = 512` (mới nâng từ 128 ở cuối Tuần 3 khi thêm TCP Reno) | Đã mở rộng nhưng chưa có bộ đếm theo dõi mức rảnh thực tế dưới tải nhiều kết nối | `kernel/mbuf.c` |
| Thống kê & Low Watermark | Chưa có `mbuf_free_count`, `min_free`, hay cảnh báo dưới ngưỡng 10% | Không quan sát được áp lực bộ đệm và không kiểm chứng tự động được việc rò rỉ `mbuf` | `kernel/mbuf.c` |
| Khởi tạo metadata ở `mbufalloc_wait()` | Không reset `m->retransmitted` và `m->send_tick` (khác với `mbufalloc()`) | Cờ `retransmitted = 1` từ lần sử dụng trước có thể sót lại khi tái cấp phát, làm sai lệch Quy tắc Karn của Tuần 3 | `kernel/mbuf.c` |

---

## 3. Chi Tiết Triển Khai

### 3.1. Đánh giá dung lượng `mbuf_pool`, bộ đếm `mbuf_free_count` và cảnh báo Low Watermark (`kernel/mbuf.c`)
* **Phân tích định lượng nhu cầu `mbuf` cho 8 HTTP workers đồng thời (với TCP Reno/NewReno):**
  - RX ring của E1000 giữ thường trực: `32` `mbuf`.
  - TX ring của E1000 đang bay (in-flight): tối đa `31` `mbuf` (thực tế thường `0–2` nhờ cơ chế thu hồi sớm).
  - Hàng đợi truyền lại `retransmit_q` mỗi kết nối: khi `cwnd` mở rộng từ `16 × MSS` lên tới `64 KB` (~46 segment mỗi kết nối), 8 kết nối đồng thời cần từ `128` tới `368` `mbuf`.
  - Các `mbuf` tạm thời (đang xử lý ở `eth_input`/`tcp_input`, gói ACK/ARP, `sock_write` đang copy từ userspace): khoảng `16` `mbuf`.
  - **Tổng nhu cầu đỉnh:** khoảng `207–447` `mbuf`. Với pool cũ `128`, hệ thống chắc chắn cạn pool; kích thước `#define MBUF_POOL_SIZE 512` (`#define NMBUF MBUF_POOL_SIZE`) đáp ứng đủ đỉnh tải kèm dự phòng ~15%. Mỗi `struct mbuf` nặng khoảng 2.1 KB, nên 512 `mbuf` chiếm ~1.05 MB trên tổng 128 MB RAM của xv6 (~0.8% RAM).
* **Theo dõi thống kê và cảnh báo Low Watermark (10%):**
  - Bổ sung vào `mbuf_pool` (được bảo vệ bởi `mbuf_pool.lock`):
    - `mbuf_free_count`: số `mbuf` đang rảnh trong `free_list` (khởi tạo bằng `NMBUF = 512`).
    - `min_free`: mức `mbuf_free_count` thấp nhất từng ghi nhận từ lúc boot.
    - `alloc_fail`, `wait_sleeps`, `double_free`: số lần `mbufalloc()` thất bại, số lần `mbufalloc_wait()` phải ngủ chờ, và số lần phát hiện giải phóng trùng (`ref_cnt <= 0`).
  - Định nghĩa ngưỡng `#define MBUF_LOWAT (NMBUF / 10)` (51 `mbuf`, tức 10%) và ngưỡng hồi phục có trễ (hysteresis) `#define MBUF_LOWAT_CLEAR (NMBUF / 4)` (128 `mbuf`, tức 25%):
    - Khi `mbuf_free_count < MBUF_LOWAT` và `lowat_active == 0`: bật `lowat_active = 1` và xuất cảnh báo `[mbuf] WARN low watermark` ra console.
    - Khi `lowat_active == 1` và `mbuf_free_count >= MBUF_LOWAT_CLEAR`: tắt `lowat_active = 0` và in thông báo `[mbuf] recovered`. Cơ chế hai ngưỡng giúp không bị in lặp liên tục khi số `mbuf` dao động quanh mốc 10%.
  - Khi `mbuf_free_count` hồi phục về đúng mức rảnh nền (`idle_free = NMBUF - RX_RING_SIZE = 480`) sau một đợt cấp phát (`burst_allocs >= MBUF_IDLE_BURST_MIN` với khoảng cách tối thiểu 5 tick, hoặc `burst_allocs > 0` sau khoảng lặng `MBUF_IDLE_QUIET_TICKS = 10` tick), kernel in dòng `[mbuf] idle: free=480/512, burst_used=..., min_free=..., alloc_fail=...` để phục vụ kiểm toán rò rỉ bộ nhớ tự động.
  - **Tránh deadlock với `printk`:** Mọi thông tin cần in được chụp vào `struct mbuf_snap` khi đang giữ `mbuf_pool.lock`, và chỉ gọi `printk()` sau khi đã `release(&mbuf_pool.lock)`.

### 3.2. Đồng nhất khởi tạo metadata trong `mbufalloc_wait()` và chặn Double Free (`kernel/mbuf.c`)
* Tách hàm nội bộ `mbuf_take_locked()` dùng chung cho cả `mbufalloc()` và `mbufalloc_wait()`, đảm bảo luôn reset đầy đủ `seq = 0`, `seq_end = 0`, `retransmitted = 0`, `send_tick = 0`, `ref_cnt = 1`.
* Trong `mbuffree()`, nếu phát hiện `m->ref_cnt <= 0` (buffer đã nằm trong `free_list`), hàm tăng `double_free`, cảnh báo `[mbuf] BUG double free ignored` và thoát ngay thay vì đẩy trùng vào danh sách liên kết đơn (tránh làm hỏng vòng `free_list` và làm lệch `mbuf_free_count`).

### 3.3. Nâng Ring Buffer E1000 lên 32 Descriptor và Thu hồi sớm TX (`kernel/e1000.c`)
* Nâng `TX_RING_SIZE` và `RX_RING_SIZE` từ `16` lên `32` descriptor (thỏa mãn yêu cầu `TDLEN`/`RDLEN` là bội số 128 byte của phần cứng Intel 82540EM). Sau khi `e1000_init()` cấp phát 32 `mbuf` cho RX ring, số `mbuf` rảnh ở trạng thái nghỉ là `512 - 32 = 480`.
* Thay thế cơ chế đọc thanh ghi `TDT` trực tiếp và giải phóng trễ bằng 3 biến chỉ số phần mềm (bảo vệ bởi `e1000_lock`):
  - `tx_tail`: vị trí descriptor tiếp theo sẽ ghi.
  - `tx_clean`: vị trí descriptor cũ nhất chưa được thu hồi.
  - `tx_inflight`: số descriptor đang giữ `mbuf` trên TX ring (tối đa `TX_RING_SIZE - 1 = 31` vì phần cứng coi `TDH == TDT` là ring rỗng).
* Hiện thực hàm `e1000_tx_reclaim_locked()`:
  - Quét từ `tx_clean` trong khi `tx_inflight > 0` và bit `E1000_TXD_STAT_DD` đã được card mạng bật.
  - Giải phóng ngay `tx_mbufs[tx_clean]` về `mbuf_pool` bằng `mbuffree()`, cập nhật `tx_clean` và giảm `tx_inflight`.
* Trong `e1000_transmit()`:
  - Gọi `e1000_tx_reclaim_locked()` ngay đầu hàm trước khi kiểm tra chỗ trống.
  - Nếu ring vẫn đầy (`tx_inflight >= TX_RING_SIZE - 1`), poll tối đa `TX_FULL_SPIN = 64` lần để chờ card hoàn tất DMA rồi mới báo đầy (`tx_ring_full++`), giúp tránh drop gói oan khi gửi burst liên tiếp.
  - Thêm rào chắn bộ nhớ `__sync_synchronize()` trước khi ghi thanh ghi `E1000_TDT` và `E1000_RDT` để đảm bảo dữ liệu descriptor đã nằm trong RAM trước khi phần cứng DMA đọc.

### 3.4. Chống rơi gói ở `e1000_recv()` và mở rộng xử lý ngắt `e1000_intr()` (`kernel/e1000.c`)
* **Thu hồi cứu viện trong `e1000_recv()`:** Khi `mbufalloc(0)` thất bại lúc cần cấp `mbuf` mới bù vào RX ring, `e1000_recv()` khóa `e1000_lock`, gọi `e1000_tx_reclaim_locked()` để thu hồi các `mbuf` đã gửi xong trên TX ring về pool rồi thử `mbufalloc(0)` lại một lần nữa trước khi chấp nhận drop gói (`rx_nombuf++`).
* **Mở rộng mặt nạ ngắt `E1000_IMS` và vòng lặp `e1000_intr()`:**
  - Bật thêm các nguồn ngắt: `E1000_ICR_RXDMT0` (cảnh báo số RX descriptor trống xuống dưới ngưỡng tối thiểu), `E1000_ICR_RXO` (RX overrun), và `E1000_ICR_TXQE` (hàng đợi TX trống -> gọi `e1000_tx_reclaim_locked()` để trả hết các `mbuf` cuối cùng của một đợt truyền về pool ngay cả khi không còn gói mới gọi `e1000_transmit()`).
  - Trong `e1000_intr()`, đọc thanh ghi `E1000_ICR` tối đa 4 vòng liên tiếp cho tới khi `icr == 0`, tránh bỏ sót gói mới đến đúng lúc CPU đang xử lý `eth_input()`.

### 3.5. Kịch bản kiểm thử tải đồng thời (`tests/concurrent_stress.py`)
* Xây dựng script tự động khởi động QEMU, bật `httpd`, bắn đồng thời nhiều tiến trình `curl` (xen kẽ tải `largefile.bin` 1 MB và `README` 6,781 bytes) qua nhiều đợt liên tiếp, sau đó phân tích log console của kernel để kiểm tra:
  1. 100% kết nối nhận HTTP `200` và đúng kích thước byte.
  2. Không có `panic` hoặc `[mbuf] BUG double free`.
  3. Sau khi đợt tải cuối cùng kết thúc, dòng `[mbuf] idle` báo `free = 480/512` (`NMBUF - RX_RING_SIZE`), chứng minh 0 rò rỉ `mbuf`.

---

## 4. Kết Quả Kiểm Thử

Môi trường: QEMU `virt` (3 harts, 128 MB RAM), card mạng E1000 (`hostfwd tcp::20080-:80`), Host dùng `curl` và Python 3 (đã rebase trên nhánh `origin/main` mới nhất có TCP Reno/NewReno).

### 4.1. Khởi động hệ thống và kiểm tra trạng thái nền
Khi boot và chạy `httpd`:
```
PCI found dev 1: vendor/dev=0x100e8086
Configuring E1000 on dev 1...
e1000: rx_ring=32 tx_ring=32
```
Ngay sau khi cấp 32 `mbuf` cho RX ring, số `mbuf` rảnh nền là `512 - 32 = 480`.

### 4.2. Bộ test trạng thái tự động (`tests/run_tests.py`, loss 0%)
Chạy lại toàn bộ 6 bài kiểm thử tự động (`3way_handshake`, `closed_port_rst`, `half_close`, `fragmented_stream`, `largefile_1mb`, `client_abort`): **6/6 PASS**, không có hồi quy.

### 4.3. Kiểm thử tải đồng thời (`tests/concurrent_stress.py`, 5 kết nối đồng thời × 3 đợt, loss 0%)
Với 5 kết nối đồng thời mỗi đợt (3 kết nối tải `largefile.bin` 1 MB + 2 kết nối tải `README` 6,781 bytes, tổng cộng 15 kết nối trên 3 đợt):

| Đợt | Kết nối 1 (`largefile.bin`) | Kết nối 2 (`README`) | Kết nối 3 (`largefile.bin`) | Kết nối 4 (`README`) | Kết nối 5 (`largefile.bin`) |
| :---: | :---: | :---: | :---: | :---: | :---: |
| 1 | 200 (1 MB, 0.47 s) | 200 (6,781 B, 0.93 s) | 200 (1 MB, 0.92 s) | 200 (6,781 B, 0.47 s) | 200 (1 MB, 1.35 s) |
| 2 | 200 (1 MB, 0.95 s) | 200 (6,781 B, 0.019 s) | 200 (1 MB, 0.49 s) | 200 (6,781 B, 0.025 s) | 200 (1 MB, 1.40 s) |
| 3 | 200 (1 MB, 0.45 s) | 200 (6,781 B, 1.38 s) | 200 (1 MB, 1.38 s) | 200 (6,781 B, 0.90 s) | 200 (1 MB, 0.90 s) |

* **Tỷ lệ hoàn thành:** **15/15 request thành công (100%, tổng thời gian 4.1 s)**.
* **Kiểm toán bộ nhớ `mbuf` sau mỗi đợt tải:**
  - Log console kernel ghi nhận sau đợt cuối:
    `[mbuf] idle: free=480/512, burst_used=7, min_free=473, alloc_fail=0`
  - Giá trị `free = 480/512` khớp tuyệt đối với `NMBUF - RX_RING_SIZE = 512 - 32 = 480` (0 `mbuf` bị rò rỉ hay bỏ quên trên TX ring / `retransmit_q`).
  - Mức `min_free` thấp nhất trong suốt 3 đợt là `473/512` (tức chỉ dùng đỉnh 7–10 `mbuf` ngoài RX ring nhờ `e1000_tx_reclaim_locked()` và ngắt `TXQE` thu hồi tức thì).
  - Các bộ đếm lỗi: `alloc_fail = 0`, `Low watermark = 0`, `RX drop = 0`, `RX overrun = 0`, `TX ring full = 0`.

### 4.4. Ghi nhận khi thử nghiệm với 8 kết nối đồng thời (`--conns 8`)
Khi chạy `tests/concurrent_stress.py --conns 8` trên cả bản gốc (`HEAD`) lẫn bản Tuần 4:
* Đúng **5/8** kết nối mỗi đợt hoàn thành nhanh (~0.5–1.6 s), còn **3/8** kết nối bị treo tới hết timeout.
* Ngay cả khi có 3 kết nối client timeout, sau khi đóng kết nối thì `mbuf_pool` vẫn hồi phục chính xác về mức nền (`NMBUF - RX_RING_SIZE`).
* **Nguyên nhân gốc (thuộc phạm vi TV2 & TV3):**
  1. `user/httpd.c` (TV3) hiện vẫn là tiến trình đơn (chưa có Pre-fork Worker Pool) và đặt `listen(listen_fd, 5)` (`backlog_max = 5`).
  2. Trong `kernel/tcp_input.c` (TV2), khi gói ACK cuối của bắt tay 3 bước đến ở trạng thái `TCP_SYN_RCVD`, PCB được chuyển sang `TCP_ESTABLISHED` nhưng nếu `ls->backlog_cnt >= ls->backlog_max` (vượt quá 5 kết nối đang chờ `accept()`), kernel lặng lẽ bỏ qua việc cấp `newsock` và không đưa vào `backlog_q`. Kết quả là 3 kết nối thứ 6, 7, 8 đã bắt tay xong ở phía client nhưng không bao giờ được `accept()` lấy ra phục vụ.

---

## 5. Tồn Đọng & Việc Tiếp Theo

* **Phối hợp TV2 và TV3 để mở khóa kịch bản 8+ kết nối đồng thời (`ab -n 200 -c 8`):**
  - TV3 cần hoàn thiện Pre-fork Worker Pool (4–8 workers) và nâng `listen(listen_fd, 16)` trong `user/httpd.c`.
  - TV2 cần xử lý trường hợp `backlog_cnt >= backlog_max` ở `TCP_LISTEN`/`TCP_SYN_RCVD` trong `kernel/tcp_input.c` (bỏ gói SYN/ACK để client tự truyền lại thay vì chuyển sang `TCP_ESTABLISHED` rồi bỏ rơi khỏi `backlog_q`).
* Sau khi TV2 và TV3 hoàn tất phần việc Tuần 4, chạy lại `python3 tests/concurrent_stress.py --conns 8 --rounds 5` và `ab -n 200 -c 8` để đo lại `min_free` khi cả 8 workers cùng bơm dữ liệu song song.
* Chuẩn bị cho **Tuần 5**: nghiên cứu thanh ghi bộ đếm thời gian phần cứng RISC-V (`r_time()`, `rdcycle()`) để cung cấp đồng hồ độ phân giải microsecond phục vụ thuật toán Google BBR.

---

## 6. Danh Sách Tệp Thay Đổi

| Tệp | Thay đổi |
| :--- | :--- |
| `kernel/mbuf.c` | Định nghĩa `NMBUF = MBUF_POOL_SIZE` (512), `MBUF_LOWAT`, `MBUF_LOWAT_CLEAR`; thêm `mbuf_free_count`, `min_free`, `alloc_fail`, `wait_sleeps`, `double_free`; cảnh báo Low Watermark kèm hysteresis và báo cáo hồi phục về mức idle; đồng nhất reset `retransmitted`/`send_tick` trong `mbufalloc_wait()`; chặn double free |
| `kernel/e1000.c` | Nâng `TX_RING_SIZE = RX_RING_SIZE = 32`; quản lý TX bằng `tx_tail`/`tx_clean`/`tx_inflight` và thu hồi sớm qua `e1000_tx_reclaim_locked()`; bật ngắt `RXDMT0`, `RXO`, `TXQE` và quét lặp `ICR`; thu hồi cứu viện TX khi `e1000_recv()` thiếu `mbuf`; thêm rào chắn bộ nhớ DMA và bộ đếm thống kê `e1000_stats` |
| `tests/concurrent_stress.py` | Kịch bản kiểm thử tải đồng thời nhiều kết nối qua nhiều đợt và kiểm toán tự động `mbuf_free_count` hồi phục 100% về mức nền |
