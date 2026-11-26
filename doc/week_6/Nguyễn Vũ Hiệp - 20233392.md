# TIẾN TRÌNH TUẦN 6 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai: Hệ Thống Kiểm Toán Bộ Nhớ Mạng (`memaudit`), Vá Rò Rỉ `retransmit_q` / PCB Zombie Khi Mất Gói Cực Đoan & Stress Test 20%–30%–50% Zero Memory Leak

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 6 (Week 6)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục tiêu

1. Xây dựng cơ chế **kiểm toán bộ nhớ mạng toàn diện (Kernel Network Memory Audit)** xuyên suốt 4 tầng (`mbuf_pool`, driver `e1000`, bảng `arp_cache`, và `tcp_mgr`), đối soát độc lập 3 nguồn dữ liệu (`mbuf_free_count`, duyệt danh sách liên kết `free_list`, và quét `ref_cnt` trên toàn mảng `mbufs[512]`).
2. Cung cấp syscall `SYS_memaudit` (mã 34) và công cụ dòng lệnh `user/memaudit.c` hỗ trợ kiểm toán trực tiếp trên xv6 (`memaudit`, `memaudit -w <giây>`, `memaudit -f`, `memaudit -k`).
3. Rà soát và triệt tiêu toàn bộ các đường rò rỉ `mbuf` và `tcp_pcb` trong các tình huống kết nối đứt gãy bất thường dưới tỷ lệ mất gói cao (nhận cờ `RST` khi `retransmit_q` còn gói, hoặc khi `MAX_BACKOFF` Circuit Breaker kích hoạt trên PCB mồ côi đã đóng socket).
4. Tối ưu khả năng phục hồi đa lỗ hổng trong cùng cửa sổ truyền (Post-Timeout Partial ACK & DupACK Retransmit Backup) và thực hiện bài **Stress Test khắc nghiệt** ở các mức mất gói **20%, 30%, 50%**, kết hợp **Client Abort giữa chừng** và **8 kết nối tải đồng thời**, chứng minh **0% rò rỉ bộ nhớ (`leaked = 0`, `zombie = 0`, `512/512` `mbuf` bảo toàn 100%)**.

---

## 2. Hiện trạng đầu tuần

Qua rà soát mã nguồn đầu Tuần 6 (sau commit `4b731de`) và chạy thử nghiệm ép tải ở tỷ lệ mất gói cao (20%–50%) kèm ngắt kết nối đột ngột (`curl` bị kill giữa chừng), nhóm phát hiện các lỗ hổng rò rỉ tài nguyên và điểm nghẽn phục hồi sau:

| Hạng mục | Hiện trạng đầu tuần | Hệ quả khi Stress Test khắc nghiệt | Vị trí |
| :--- | :--- | :--- | :--- |
| Kiểm toán bộ nhớ chủ động | Chỉ có log thụ động `[mbuf] idle:` khi một đợt cấp phát vừa trả buffer về mức nền | Không thể chủ động đối soát từng tầng (`e1000`, `arp`, `tcp`) hay kiểm tra tính toàn vẹn của `free_list` và `ref_cnt` theo yêu cầu | `kernel/mbuf.c`, `kernel/e1000.c`, `kernel/arp.c`, `kernel/tcp.c` |
| Xử lý gói `RST` trong `tcp_input()` | Khi nhận `RST`, kernel chỉ giải phóng `pcb->ooo_queue` nhưng **quên không giải phóng `pcb->retransmit_q`** | Toàn bộ các bản sao `mbuf` đang chờ ACK trong `retransmit_q` bị bỏ quên vĩnh viễn khi client gửi `RST` hủy tải file 1 MB giữa chừng | `kernel/tcp_input.c` |
| PCB mồ côi (`pcb->sock == 0`) nhận `RST` hoặc chạm `MAX_BACKOFF` | Khi `sock_close()` đã chạy xong và tách `pcb->sock = 0` (PCB ở `FIN_WAIT_1`, `FIN_WAIT_2`, `LAST_ACK`, `TIME_WAIT`), nếu nhận `RST` hoặc vượt `MAX_BACKOFF`, kernel chỉ gán `pcb->state = TCP_CLOSED` nhưng **không gọi `tcp_pcb_free(pcb)`** | PCB nằm ở trạng thái `TCP_CLOSED` nhưng vẫn kẹt trong `hash_table` và không bao giờ quay về `tcp_mgr.free_list` (rò rỉ **Zombie PCB** làm cạn pool 32 PCB) | `kernel/tcp_input.c`, `kernel/tcp.c` |
| Phục hồi sau RTO Timeout khi mất nhiều gói trong cùng cửa sổ (loss 20%–30%) | `tcp_cc_on_timeout()` đặt `dupack_cnt = 0` và `recover = snd_nxt`; khi gói đầu hàng đợi được ACK nhưng vẫn còn lỗ hổng khác trong `[ack, recover)`, `tcp_cc_on_ack()` không truyền lại ngay mà đợi hết RTO 200 ms cho từng lỗ hổng tiếp theo | Với cửa sổ 16 segments mất 4–5 gói ở loss 30%, mỗi cửa sổ bị trễ nối tiếp nhiều chu kỳ RTO làm thời gian tải 1 MB kéo dài > 120–180 giây | `kernel/tcp_cc.c` |

---

## 3. Chi Tiết Triển Khai

### 3.1. Phương trình bảo toàn `mbuf` và Kiến trúc Kiểm toán 4 tầng (`kernel/net.h`, `kernel/mbuf.c`, `kernel/e1000.c`, `kernel/arp.c`, `kernel/tcp.c`)

* **Nguyên lý bảo toàn bất biến (Conservation Invariant):**
  Toàn bộ hệ thống mạng của xv6 sử dụng một vùng nhớ tĩnh duy nhất `mbuf_pool.mbufs[NMBUF]` với `NMBUF = 512`. Tại mọi thời điểm bất kỳ (kể cả khi đang truyền tải hay ở trạng thái nghỉ), mỗi `mbuf` bắt buộc phải thuộc về **đúng một** trong 6 tập hợp rời nhau:
  $$N_{\text{accounted}} = N_{\text{free}} + N_{\text{rx\_ring}} + N_{\text{tx\_ring}} + N_{\text{arp\_pending}} + N_{\text{tcp\_rtx}} + N_{\text{tcp\_ooo}}$$
  Số lượng `mbuf` rò rỉ được xác định bởi:
  $$N_{\text{leaked}} = \text{NMBUF} - N_{\text{accounted}}$$
  Ở trạng thái nghỉ sau khi đóng toàn bộ kết nối:
  $$N_{\text{tx\_ring}} = N_{\text{arp\_pending}} = N_{\text{tcp\_rtx}} = N_{\text{tcp\_ooo}} = 0 \implies N_{\text{free}} + N_{\text{rx\_ring}} = 480 + 32 = 512 = \text{NMBUF}$$

* **Cấu trúc biên bản kiểm toán `struct net_audit` (`kernel/net.h`, `user/user.h`):**
  1. **Kiểm toán Pool `mbuf` (`mbuf_audit` trong `kernel/mbuf.c`):**
     - Đối chiếu 3 đại lượng độc lập dưới `mbuf_pool.lock`:
       - Biến đếm `mbuf_free_count`.
       - Số nút thực tế `mbuf_free_walk` khi duyệt con trỏ `free_list` (phát hiện đứt gãy hoặc chu trình vòng lặp do lỗi con trỏ).
       - Số phần tử `mbuf_refcnt_used` có `ref_cnt > 0` khi quét toàn bộ mảng `mbufs[0..511]`, đồng thời kiểm tra `mbuf_bad` (`ref_cnt != 0` trong `free_list` hoặc `ref_cnt < 0`).
     - Điều kiện nhất quán nội tại của pool:
       $$\text{mbuf\_free\_walk} = \text{mbuf\_free\_count} \quad \text{và} \quad \text{mbuf\_refcnt\_used} = \text{NMBUF} - \text{mbuf\_free\_count}$$
  2. **Kiểm toán Driver NIC E1000 (`e1000_audit` trong `kernel/e1000.c`):**
     - Chủ động gọi `e1000_tx_reclaim_locked()` dưới `e1000_lock` để thu hồi ngay mọi descriptor TX mà card mạng đã truyền xong (`E1000_TXD_STAT_DD = 1`) về `mbuf_pool`.
     - Đếm trực tiếp số slot đang giữ `mbuf` trên `rx_mbufs[32]` (`rx_ring_held = 32`) và `tx_mbufs[32]` (`tx_inflight`), kèm thống kê `tx_packets`, `tx_loss_sim`, `tx_ring_full`, `rx_packets`, `rx_nombuf`, `loss_rate`.
  3. **Kiểm toán Bảng ARP (`arp_audit` và `arp_flush` trong `kernel/arp.c`):**
     - `arp_audit()` duyệt 16 slot của `arp_cache`, đếm số bản ghi hợp lệ (`arp_valid`), số slot đang chờ phân giải (`arp_pending_entries`) và tổng số gói tin đang xếp hàng trong `pending_queue` (`arp_pending_mbufs`).
     - `arp_flush()` cho phép xóa sạch toàn bộ bảng ARP (gọi `arp_drop_pending()` giải phóng mọi `pending_queue` và đặt `valid = 0`) khi truyền cờ `NETAUDIT_FLUSH_ARP` (`memaudit -f`).
  4. **Kiểm toán Tầng TCP & Phát hiện Zombie PCB (`tcp_audit` trong `kernel/tcp.c`):**
     - Đánh dấu các PCB đang nằm trong `tcp_mgr.free_list` (`in_free[i] = 1`).
     - Quét toàn bộ 32 PCB trong `tcp_mgr.pcbs[]`: đếm tổng số `mbuf` trong `retransmit_q` (`tcp_rtx_mbufs`) và `ooo_queue` (`tcp_ooo_mbufs`), phân loại trạng thái (`tcp_listen`, `tcp_conns`, `tcp_time_wait`), và phát hiện **Zombie PCB** (`!in_free[i] && pcb->state == TCP_CLOSED && pcb->sock == 0`).

### 3.2. Vá triệt để rò rỉ `retransmit_q` và Zombie PCB khi nhận `RST` hoặc Circuit Breaker (`kernel/tcp_input.c`, `kernel/tcp.c`)

* **Trong nhánh xử lý cờ `TCP_FLAG_RST` tại `tcp_input()` (`kernel/tcp_input.c`):**
  1. Bổ sung vòng lặp giải phóng toàn bộ `pcb->retransmit_q` và đặt `pcb->rto_deadline = 0` ngay khi kết nối chuyển sang `TCP_CLOSED` (trước đây chỉ giải phóng `ooo_queue` khiến toàn bộ các segment đang chờ truyền lại bị rò rỉ vì `tcp_timer_process()` bỏ qua PCB ở trạng thái `TCP_CLOSED`).
  2. Nếu PCB đã mồ côi (`pcb->sock == 0`, tức ứng dụng đã đóng socket hoặc PCB `TCP_SYN_RCVD` vừa được gỡ khỏi `syn_q`), gọi ngay `tcp_pcb_free(pcb)` sau khi nhả `pcb->lock` để trả PCB về `tcp_mgr.free_list`.
* **Trong nhánh Circuit Breaker (`pcb->backoff > MAX_BACKOFF`) tại `tcp_timer_process()` (`kernel/tcp.c`):**
  - Khi kết nối bị hủy do vượt quá 12 lần truyền lại liên tiếp, nếu `pcb->sock == 0` (ví dụ socket đã đóng và PCB đang ở `FIN_WAIT_1` hoặc `LAST_ACK` nhưng gói `FIN` bị mất liên tiếp), hàm gọi `tcp_pcb_free(pcb)` để gỡ PCB khỏi bảng băm và hoàn trả về `free_list`, ngăn chặn triệt để hiện tượng rò rỉ Zombie PCB.

### 3.3. Tăng tốc phục hồi đa lỗ hổng ở mức mất gói 20%–30% (`kernel/tcp_cc.c`)

* **Khởi tạo chuẩn mốc `pcb->recover = pcb->iss`** trong `tcp_cc_init()`.
* **Phục hồi Partial ACK sau RTO Timeout (`tcp_cc_on_ack`):** Khi một cửa sổ truyền lớn (16 segments) bị mất nhiều gói ở mức loss 20%–30% và xảy ra RTO timeout (`recover = snd_nxt`), ngay khi gói đầu tiên được ACK mà vẫn thỏa `(int32)(ack - pcb->recover) < 0` và `pcb->retransmit_q != 0`, kernel gọi ngay `tcp_fast_retransmit(pcb)` để truyền lại lỗ hổng kế tiếp trong cùng cửa sổ mà không phải chờ thêm 200 ms cho mỗi gói mất.
* **Truyền lại dự phòng khi gói Fast Retransmit bị drop (`tcp_cc_on_dup_ack`):** Khi `dupack_cnt > 3` và `(pcb->dupack_cnt % 3) == 0`, gọi lại `tcp_fast_retransmit(pcb)` để khôi phục nhanh nếu bản tin Fast Retransmit đầu tiên (ở `dupack_cnt == 3`) vô tình bị bộ giả lập mất gói đánh rơi.
* **Hiệu quả:** Thời gian tải file 1 MB (`largefile.bin`) ở mức mất gói **20%** giảm từ **> 120 giây (timeout)** xuống chỉ còn **0.89 – 1.70 giây**; ở mức mất gói **30%** hoàn tất 100% trong **28.07 – 40.42 giây** với mã SHA-256 khớp tuyệt đối.

### 3.4. Syscall `SYS_memaudit` và Tiện ích `user/memaudit.c` (`kernel/sysnet.c`, `user/memaudit.c`)

* Đăng ký syscall `SYS_memaudit` (mã 34):
  - Nếu `uaddr == 0`: in biên bản kiểm toán trực tiếp từ kernel ra console qua `net_audit_print()`.
  - Nếu `uaddr != 0`: sao chép `struct net_audit` ra user-space và trả về số `mbuf` bị rò rỉ (`a.leaked`).
* Xây dựng công cụ `user/memaudit.c`:
  - `memaudit`: kiểm toán tức thời và in biên bản đối chiếu.
  - `memaudit -w <giây>`: chờ tối đa `<giây>` cho tới khi ngăn xếp về trạng thái nghỉ (`quiescent`: không còn kết nối hoạt động ngoài `TIME_WAIT`, `tx_inflight == 0`, `rtx == 0`, `ooo == 0`) rồi chốt biên bản kiểm toán.
  - `memaudit -f`: xóa sạch bảng ARP trước khi chốt biên bản kiểm toán.
  - `memaudit -k`: yêu cầu kernel dump biên bản `[memaudit]` trực tiếp ra console.

---

## 4. Kết Quả Kiểm Thử

Môi trường: QEMU `virt` (3 harts, 128 MB RAM), card mạng Intel E1000 (`hostfwd tcp::20080-:80`), biên dịch sạch 100% với `-Wall -Werror`.

### 4.1. Kịch bản Stress Test Tổng Hợp Tuần 6 (`python3 tests/test_week6_zero_leak.py --rounds 2 --conns 8`)

Kịch bản thực thi liên hoàn 6 pha thử thách khắc nghiệt trên cùng một phiên chạy xv6 (không khởi động lại giữa các pha) và gọi `memaudit` kiểm toán bộ nhớ sau từng pha:

```text
  [memaudit] nen luc boot               free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=0 -> PASS
Pha mat goi 20%: tai largefile.bin (1 MB) x2
  lan 1: http=200 size=1048576 time=1.70s sha256=KHOP
  lan 2: http=200 size=1048576 time=0.89s sha256=KHOP
  [memaudit] sau loss 20% (1MB)         free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=428 -> PASS
Pha mat goi 30%: tai largefile.bin (1 MB) x2
  lan 1: http=200 size=1048576 time=28.07s sha256=KHOP
  lan 2: http=200 size=1048576 time=40.42s sha256=KHOP
  [memaudit] sau loss 30% (1MB)         free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=1087 -> PASS
Pha mat goi 50%: tai README (6781 B) x2
  lan 1: http=200 size=6781 time=1.00s OK
  lan 2: http=200 size=6781 time=0.19s OK
  [memaudit] sau loss 50% (README)      free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=1100 -> PASS
Pha client abort (loss 30%): kill curl giua chung x3
  lan 1: da kill curl sau 0.6s
  lan 2: da kill curl sau 0.9s
  lan 3: da kill curl sau 1.2s
  [memaudit] sau client abort           free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=1249 -> PASS
Pha tai song song (loss 20%): 8 curl dong thoi
  thanh cong 8/8 trong 4.0s
  [memaudit] sau tai song song 20%      free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=1999 -> PASS
Kiem tra hau stress (loss 0%): http=200 size=1048576 time=0.45s KHOP SHA-256
  [memaudit] bien ban cuoi (-f)         free=480/512 walk=480 rx_ring=32 accounted=512/512 conns=0 tw=0 zombie=0 rtx=0 ooo=0 tx_loss_sim=1999 -> PASS
```

### 4.2. Biên bản kiểm toán `memaudit` xuất trực tiếp từ console xv6 sau hơn 16,700 gói tin

```text
$ memaudit -f -w 60
memaudit: cho 0 ms de ngan xep ve trang thai nghi (nghi)
== memaudit: kiem toan bo nho ngan xep mang ==
mbuf pool : total=512 free=480 (walk=480) in_use=32 refcnt_used=32 bad=0 min_free=431
            alloc_fail=0 wait_sleeps=0 double_free=0
e1000     : rx_ring=32 tx_inflight=0 loss=0% tx_pkts=7367 tx_loss_sim=1999 tx_ring_full=0 rx_pkts=7393 rx_nombuf=0
arp       : valid=0 pending_entries=0 pending_mbufs=0
tcp       : pcb_free=31/32 listen=1 conns=0 time_wait=0 zombie=0 rtx_mbufs=0 ooo_mbufs=0
doi chieu : free(480) + rx_ring(32) + tx(0) + arp(0) + rtx(0) + ooo(0) = 512/512
hoi phuc  : free + rx_ring = 512/512 -> 100% (nguyen ven)
memaudit: PASS leaked=0 zombie=0 (zero memory leak)

$ memaudit -k
[memaudit] mbuf: total=512 free=480 walk=480 refcnt_used=32 bad=0 min_free=431 alloc_fail=0 double_free=0
[memaudit] e1000: rx_ring=32 tx_inflight=0 loss=0% tx_loss_sim=1999 tx_ring_full=0 rx_nombuf=0
[memaudit] arp: valid=0 pending_entries=0 pending_mbufs=0
[memaudit] tcp: pcb_free=31/32 listen=1 conns=0 time_wait=0 zombie=0 rtx_mbufs=0 ooo_mbufs=0
[memaudit] accounted=512/512 leaked=0
```

### 4.3. Bảng tổng hợp số liệu kiểm toán qua từng pha Stress Test

| Pha kiểm thử | Tỷ lệ mất gói | Khối lượng tải | Tỷ lệ thành công | Thời gian | Gói drop tích lũy (`tx_loss_sim`) | `mbuf` rảnh (`free / walk`) | Đối chiếu (`accounted`) | `leaked` / `zombie` | Kết luận |
| :--- | :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **0. Khởi động (Boot)** | 0% | Nền hệ thống | — | — | `0` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |
| **1. Tải 1 MB × 2** | **20%** | `2 × 1,048,576 B` | **2/2 (100% SHA-256)** | `1.70 s`, `0.89 s` | `428` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |
| **2. Tải 1 MB × 2** | **30%** | `2 × 1,048,576 B` | **2/2 (100% SHA-256)** | `28.07 s`, `40.42 s` | `1,087` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |
| **3. Tải `README` × 2** | **50%** | `2 × 6,781 B` | **2/2 (100%)** | `1.00 s`, `0.19 s` | `1,100` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |
| **4. Client Abort × 3** | **30%** | Ngắt `curl` ở `0.6s, 0.9s, 1.2s` | **3/3 thu hồi sạch** | — | `1,249` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |
| **5. Đa kết nối (8 clients)** | **20%** | `4 × 1 MB + 4 × README` song song | **8/8 (100%)** | `4.02 s` | `1,999` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |
| **6. Hậu Stress + Flush ARP** | 0% | `1 × 1,048,576 B` + `memaudit -f` | **1/1 (100% SHA-256)** | `0.45 s` | `1,999` | `480 / 480` | `512 / 512` | `0 / 0` | **PASS** |

* **Đỉnh tiêu thụ bộ đệm:** Trong toàn bộ 6 pha (kể cả khi 8 client tải đồng thời ở mức mất gói 20%), mức `min_free` thấp nhất là **`431/512`** (tức tối đa chỉ dùng 81 `mbuf` = 15.8% pool, còn dư 84.2% dung lượng an toàn), `alloc_fail = 0`, `double_free = 0`, `tx_ring_full = 0`, `rx_nombuf = 0`.
* **Kiểm toán PCB TCP:** Sau khi đóng toàn bộ kết nối client, `pcb_free = 31/32` (đúng 1 PCB duy nhất đang `LISTEN` ở cổng 80 của `httpd`), `zombie = 0`, `rtx_mbufs = 0`, `ooo_mbufs = 0`.

### 4.4. Kiểm thử hồi quy toàn hệ thống (Regression Tests)

1. **`python3 tests/run_tests.py` (6 bài FSM & truyền tải 1 MB):** **6/6 PASS** (`test_3way_handshake`, `test_closed_port_rst`, `test_half_close`, `test_fragmented_stream`, `test_largefile_1mb`, `test_client_abort`).
2. **`python3 tests/test_http11.py` (5 bài HTTP/1.1 & Worker Pool):** **5/5 PASS** (`test_index_and_mime`, `test_favicon_204`, `test_keep_alive`, `test_long_headers`, `test_worker_pool`).
3. **`python3 tests/test_hwclock_bbr.py` (Hardware Counters & 10s `min_rtt` / `delivery_rate`):** **PASS** (`rtt_us = 48 µs`, `min_rtt_us = 44 µs`, `rate = 26,367 KB/s`, tải 1 MB trong `0.64 s`).
4. **`python3 tests/concurrent_stress.py --conns 8 --rounds 2` (16 kết nối song song):** **16/16 PASS** (`4.8 s`, pool hồi phục `free = 480/512`).
5. **`python3 tests/test_week6_zero_leak.py --rounds 2 --conns 8` (Stress Test mất gói 20%–30%–50% & Zero-Leak Audit):** **7/7 biên bản kiểm toán PASS (`leaked = 0`, `zombie = 0`)**.

---

## 5. Tổng Kết Nghiệm Thu KPI Của TV1 (Toàn Khóa 6 Tuần)

| Hạng mục KPI | Chỉ tiêu kế hoạch | Kết quả đạt được thực tế | Trạng thái |
| :--- | :--- | :--- | :---: |
| **Deadlock Prevention** | 0 lần treo máy khi tải lặp lại nhiều lần | 0 deadlock, 0 panic qua tất cả các bài stress test đơn luồng và đa luồng (`alloc_fail = 0`) | ✅ Đạt 100% |
| **Packet Loss Control** | Điều chỉnh 0% – 100% động không cần reboot | Syscall & CLI `setloss <0..100>` hoạt động tức thời trên E1000 | ✅ Đạt 100% |
| **RTO & Hardware Clock** | Chuẩn 10 Hz (`1 tick = 100 ms`) & đo RTT độ phân giải `µs` | Hiệu chuẩn `hwclock` `10 MHz` (`0.1 µs`), `min_rtt` cửa sổ 10s & `delivery_rate` chính xác | ✅ Đạt 100% |
| **Extreme Loss Resilience** | Tải thành công & tự phục hồi ở mức loss 20%, 30%, 50% | Tải 1 MB ở 20% loss (`0.89–1.70 s`), 30% loss (`28–40 s`), 50% loss (`README` `0.19–1.00 s`) khớp 100% SHA-256 | ✅ Đạt 100% |
| **Zero Memory Leak** | `0` memory leak sau hàng nghìn gói tin & kết nối | `memaudit` xác nhận `free(480) + rx_ring(32) = 512/512`, `leaked = 0`, `zombie = 0` sau > 16,700 gói | ✅ Đạt 100% |

---

## 6. Danh Sách Tệp Thay Đổi Trong Tuần 6

| Tệp | Thay đổi |
| :--- | :--- |
| `kernel/net.h` | Định nghĩa `struct net_audit`, cờ `NETAUDIT_FLUSH_ARP` và khai báo các hàm kiểm toán `mbuf_free_count()`, `mbuf_audit()`, `e1000_audit()`, `arp_audit()`, `arp_flush()`, `tcp_audit()`, `net_audit_collect()` |
| `kernel/mbuf.c` | Hiện thực `mbuf_free_count()` và `mbuf_audit()` đối soát 3 chiều (`mbuf_free_count`, duyệt `free_list`, quét `ref_cnt` trên `mbufs[512]`) |
| `kernel/e1000.c` | Hiện thực `e1000_audit()`: chủ động thu hồi TX descriptors đã hoàn tất (`e1000_tx_reclaim_locked`) và kiểm đếm `rx_ring_held`, `tx_inflight`, bộ đếm `e1000_stats` |
| `kernel/arp.c` | Hiện thực `arp_audit()` (đếm `arp_valid`, `arp_pending_entries`, `arp_pending_mbufs`) và `arp_flush()` (xóa sạch bảng ARP và `pending_queue`) |
| `kernel/tcp.c` | Hiện thực `tcp_audit()`, `net_audit_collect()` và vá lỗi rò rỉ Zombie PCB khi PCB mồ côi (`pcb->sock == 0`) chạm ngưỡng Circuit Breaker `MAX_BACKOFF` |
| `kernel/tcp_input.c` | Vá lỗi rò rỉ `pcb->retransmit_q` và Zombie PCB khi nhận gói `TCP_FLAG_RST` trên kết nối đang có gói chờ truyền lại hoặc đã đóng socket |
| `kernel/tcp_cc.c` | Tối ưu truyền lại ngay lỗ hổng kế tiếp trên Partial ACK sau RTO timeout và truyền lại dự phòng trên DupACK khi mất gói cao (20%–50%) |
| `kernel/syscall.h`, `kernel/syscall.c`, `kernel/sysnet.c` | Đăng ký và hiện thực syscall `SYS_memaudit` (mã 34) cùng hàm in biên bản kernel `net_audit_print()` |
| `user/user.h`, `user/usys.pl`, `user/memaudit.c`, `Makefile` | Khai báo wrapper `memaudit()`, xây dựng tiện ích dòng lệnh `user/memaudit.c` (`-w`, `-f`, `-k`) và tích hợp vào `UPROGS` |
| `tests/test_week6_zero_leak.py` | Kịch bản kiểm thử tự động Stress Test mất gói 20%–30%–50%, Client Abort, tải song song 8 kết nối và kiểm toán Zero Memory Leak |
