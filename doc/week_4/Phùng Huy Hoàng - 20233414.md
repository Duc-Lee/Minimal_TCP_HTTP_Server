# TIẾN TRÌNH TUẦN 4 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai: Phân Tách SYN Queue & Accept Queue, Chống Nghẽn Đa Kết Nối & Tùy Chọn TCP Window Scale (RFC 7323)

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 4 (Week 4)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Kỹ Thuật Tuần 4

Theo kế hoạch phối hợp Tuần 4 (`plans/weeks/week_4.md` và `plans/members/member_2_tv2.md`):
1. **Phân tách triệt để kiến trúc SYN Queue và Accept Queue trong TCP/Socket Layer:**
   - Thay thế cơ chế hàng đợi đơn sơ cấp bằng hai hàng đợi chuyên biệt: **SYN Queue** (quản lý kết nối bán phần đang bắt tay 3 bước `TCP_SYN_RCVD`) và **Accept Queue** (quản lý kết nối hoàn tất `TCP_ESTABLISHED` chờ worker gọi `accept()`).
   - Chuyển đổi hàng đợi Accept từ cơ chế LIFO (Last-In First-Out) sang chuẩn **FIFO (First-In First-Out)** bằng con trỏ đuôi `backlog_tail`, loại bỏ nguy cơ đói kết nối (starvation).
2. **Khắc phục dứt điểm lỗi treo kết nối (Hanging Connections) khi chạy tải đồng thời:**
   - Giải quyết tận gốc bài toán 3 kết nối bị treo vĩnh viễn dẫn tới timeout trong kịch bản tải 8 kết nối đồng thời (`tests/concurrent_stress.py --conns 8`).
   - Ngăn chặn hoàn toàn hiện tượng "kết nối mồ côi" (Orphaned PCB) khi hàng đợi backlog bị đầy.
   - Nâng dung lượng bảng socket `MAX_SOCKETS` từ 16 lên 32 để đồng bộ trọn vẹn với kích thước `TCP_PCB_POOL_SIZE = 32`.
3. **Triển khai Tùy chọn TCP Window Scale Option (RFC 7323):**
   - Xây dựng bộ phân tích tiêu đề tùy chọn TCP (`tcp_parse_options`) hỗ trợ MSS (Kind 2) và Window Scale (Kind 3).
   - Tự động thương lượng Window Scale factor (`TCP_RCV_WSCALE = 2`) trong các segment SYN và SYN-ACK.
   - Mở rộng cửa sổ gửi nhận `snd_wnd` và `rcv_wnd` vượt qua giới hạn cổ điển 64 KB của TCP header 16-bit, tối ưu hóa thông lượng truyền tải cho trình duyệt web hiện đại (Chrome, Firefox).
4. **Kiểm thử tự động & Đảm bảo toàn vẹn hệ thống:**
   - Đảm bảo 100% vượt qua bộ test máy trạng thái FSM (`tests/run_tests.py` - 6/6 tests passed).
   - Chạy tải đồng thời 8 kết nối qua nhiều đợt liên tiếp (`tests/concurrent_stress.py --conns 8 --rounds 3`), xác nhận 24/24 requests thành công trọn vẹn và mbuf pool thu hồi tuyệt đối `480/512` (0 rò rỉ bộ nhớ).

---

## 2. Hiện Trạng Đầu Tuần & Phân Tích Lỗi Treo Kết Nối

Bảng đối chiếu hiện trạng đầu Tuần 4 và phân tích kỹ thuật:

| Hạng mục | Hiện trạng đầu tuần | Phân tích vấn đề & Hậu quả |
| :--- | :--- | :--- |
| **Quản lý Backlog Queue** | Chỉ có 1 hàng đợi đơn `backlog_q` trong `struct socket` | Không phân biệt kết nối đang bắt tay (`SYN_RCVD`) và kết nối đã hoàn tất bắt tay (`ESTABLISHED`). Thiếu cơ chế giới hạn bán kết nối (`syn_max`) chống SYN Flood. |
| **Thứ tự hàng đợi Accept** | Chèn vào đầu `newsock->next = ls->backlog_q` (LIFO) | Kết nối đến sau lại được `accept()` phục vụ trước kết nối đến trước, làm tăng độ trễ bất thường (jitter) và gây starvation cho các kết nối cũ. |
| **Xử lý tràn Backlog** | Chuyển `pcb->state = TCP_ESTABLISHED` ngay cả khi `backlog_cnt >= backlog_max` | **Nguyên nhân gốc rễ gây treo server:** Khi `backlog_cnt` đạt ngưỡng tối đa, kernel không cấp được socket struct mới nhưng đã vội đổi PCB sang `ESTABLISHED`. PCB này bị "mồ côi" (vẫn trỏ về listen socket). Khi client gửi HTTP GET, dữ liệu bị đẩy vào `rx_buf` của listen socket; không có worker nào nhận được kết nối để phản hồi, khiến client bị treo timeout vĩnh viễn. |
| **Dung lượng socket table** | `MAX_SOCKETS = 16` cố định | Trong khi `TCP_PCB_POOL_SIZE = 32` và `MBUF_POOL_SIZE = 512`, bảng socket chỉ có 16 phần tử, dễ cạn kiệt khi có 8 kết nối đồng thời kèm các socket đang trong trạng thái đóng (`TIME_WAIT`, `CLOSE_WAIT`). |
| **Giới hạn cửa sổ TCP** | Trường `win` chỉ có 16-bit, tối đa 65,535 bytes | Không hỗ trợ RFC 7323 Window Scale. Khi kết nối với trình duyệt web hoặc client trên Host, cửa sổ truyền nhận bị bóp nghẹt ở mức nhỏ, giới hạn thông lượng đường truyền. |

---

## 3. Chi Tiết Triển Khai Kỹ Thuật

```
                    ┌──────────────────────────────────────────┐
                    │            Listen Socket (Port 80)       │
                    │         backlog_max = 16, syn_max = 32   │
                    └─────────────────────┬────────────────────┘
                                          │
                  ┌───────────────────────┴───────────────────────┐
                  │                                               │
                  ▼                                               ▼
     ┌─────────────────────────┐                     ┌─────────────────────────┐
     │    SYN Queue (syn_q)    │                     │  Accept Queue (backlog) │
     │  Trạng thái: SYN_RCVD   │  ACK (Bước 3) đến   │ Trạng thái: ESTABLISHED │
     │ Đang chờ ACK từ client  │ ──────────────────> │ Sẵn sàng cho accept()   │
     │   (Quản lý theo PCB)    │ (backlog_cnt < max) │   (Hàng đợi FIFO Socket)│
     └─────────────────────────┘                     └────────────┬────────────┘
                  │                                               │
                  │ backlog đầy (backlog_cnt >= max)              ▼
                  ▼                                        ┌───────────────┐
       Drop ACK -> Client retry                            │ Worker accept │
       (Không tạo Orphaned PCB!)                           └───────────────┘
```

### 3.1. Phân Tách SYN Queue & Accept Queue (`kernel/net.h`, `kernel/sock.c`)

1. **Mở rộng `struct socket` trong `kernel/net.h`:**
   ```c
   struct socket {
     ...
     // Hang doi listen backlog: Accept Queue (FIFO) va SYN Queue
     struct socket *backlog_q;
     struct socket *backlog_tail;  // Con tro duoi de duy tri FIFO
     struct socket *next;
     int backlog_max;
     int backlog_cnt;

     struct tcp_pcb *syn_q;        // Danh sach cac ket noi dang bat tay (SYN_RCVD)
     int syn_cnt;                  // So luong ket noi dang trong syn_q
     int syn_max;                  // Dung luong toi da cua syn_q
     ...
   };
   ```

2. **Mở rộng `struct tcp_pcb` trong `kernel/net.h`:**
   ```c
   struct tcp_pcb {
     ...
     // Tuy chon Window Scale (RFC 7323)
     uint8 snd_wscale;             // Scale factor do client de nghi (0..14)
     uint8 rcv_wscale;             // Scale factor cua xv6 (TCP_RCV_WSCALE = 2)
     uint8 wscale_ok;              // 1 neu ca 2 ben deu dong y dung Window Scale
     struct tcp_pcb *syn_next;     // Con tro moc noi trong syn_q cua listen socket
     void *sock;
   };
   ```

3. **Cơ chế Accept Queue FIFO trong `kernel/sock.c`:**
   - Khi kết nối hoàn tất bắt tay, socket con được nối vào đuôi:
     ```c
     newsock->next = 0;
     if (ls->backlog_tail) {
       ls->backlog_tail->next = newsock;
     } else {
       ls->backlog_q = newsock;
     }
     ls->backlog_tail = newsock;
     ls->backlog_cnt++;
     ```
   - Trong `sock_accept()`: lấy socket từ đầu `backlog_q`, cập nhật `backlog_tail = 0` khi hàng đợi rỗng:
     ```c
     newsock = s->backlog_q;
     s->backlog_q = newsock->next;
     if (s->backlog_q == 0)
       s->backlog_tail = 0;
     newsock->next = 0;
     s->backlog_cnt--;
     ```

4. **Nâng `MAX_SOCKETS` lên 32 và quản lý dọn dẹp hàng đợi trong `sock_close()`:**
   - Tăng `#define MAX_SOCKETS 32` đảm bảo hệ thống không bao giờ bị nghẽn socket struct khi nhiều worker và client cùng hoạt động.
   - Trong `sock_close()`, khi listen socket bị đóng, duyệt và giải phóng đồng thời cả `backlog_q` (Accept Queue) lẫn `syn_q` (SYN Queue), tránh thất thoát tài nguyên.

---

### 3.2. Cơ Chế Chống Treo Kết Nối & Bảo Vệ Quá Tải (`kernel/tcp_input.c`)

1. **Khắc phục lỗi Orphaned PCB tại `case TCP_SYN_RCVD`:**
   - Khi nhận gói ACK (bước 3 của quá trình bắt tay):
     ```c
     struct socket *ls = (struct socket *)pcb->sock;
     if (ls) {
       acquire(&ls->lock);
       // Kiem tra dung luong Accept Queue
       if (ls->backlog_cnt >= ls->backlog_max) {
         // Accept Queue da day: GIU NGUYEN trang thai TCP_SYN_RCVD trong syn_q!
         // Drop goi ACK de client gui lai sau khi worker accept bot ket noi.
         release(&ls->lock);
         release(&pcb->lock);
         mbuffree(m);
         return;
       }

       struct socket *newsock = sock_alloc_for_pcb(pcb);
       if (!newsock) {
         // Bang socket tam het: drop ACK de client retry
         release(&ls->lock);
         release(&pcb->lock);
         mbuffree(m);
         return;
       }

       // Go khoi syn_q cua listen socket
       struct tcp_pcb **curr = &ls->syn_q;
       while (*curr) {
         if (*curr == pcb) {
           *curr = pcb->syn_next;
           pcb->syn_next = 0;
           ls->syn_cnt--;
           break;
         }
         curr = &(*curr)->syn_next;
       }

       // Dua vao Accept Queue FIFO
       newsock->next = 0;
       if (ls->backlog_tail)
         ls->backlog_tail->next = newsock;
       else
         ls->backlog_q = newsock;
       ls->backlog_tail = newsock;
       ls->backlog_cnt++;
       pcb->sock = newsock;

       release(&ls->lock);
       wakeup(ls);
     }
     tcp_handle_ack(pcb, ack, win);
     pcb->state = TCP_ESTABLISHED;
     ```
   - **Bản chất giải pháp:** Nếu hàng đợi Accept đang đầy, kernel **không bao giờ** chuyển PCB sang `TCP_ESTABLISHED` một cách mù quáng. Gói ACK bị bỏ qua sẽ kích hoạt cơ chế truyền lại (TCP Retransmission) tự nhiên từ phía client (hoặc timer gửi lại SYN-ACK từ xv6). Ngay khi một worker gọi `accept()` giải phóng slot, lần retransmit tiếp theo sẽ lập tức được tiếp nhận an toàn.

2. **Hỗ trợ gói bước 3 có kèm dữ liệu (HTTP GET Data on 3rd Packet):**
   - Một số client (như `curl` tối ưu hoặc HTTP/1.1 pipeline) gửi luôn payload request ngay trong gói ACK bước 3. Đoạn mã bổ sung:
     ```c
     if (m->len > 0) {
       if (seq == pcb->rcv_nxt) {
         pcb->rcv_nxt += m->len;
         release(&pcb->lock);
         sock_push_data(pcb, m);
         tcp_output(pcb, 0, TCP_FLAG_ACK);
         return;
       }
     }
     ```
   - Đảm bảo dữ liệu HTTP được chuyển ngay vào `rx_buf` của `newsock` vừa tạo và đánh thức worker đọc dữ liệu.

3. **Bảo vệ ngắt kết nối đột ngột (RST Handling) khi đang trong `syn_q`:**
   - Trong khối xử lý `flags & TCP_FLAG_RST`, nếu PCB đang ở `TCP_SYN_RCVD`, tự động gỡ PCB ra khỏi `ls->syn_q` của listen socket trước khi chuyển sang `TCP_CLOSED`, ngăn chặn triệt để con trỏ rác (dangling pointers).

---

### 3.3. Hiện Thực Tùy Chọn TCP Window Scale Option (RFC 7323)

1. **Phân tích TCP Header Options (`tcp_parse_options`):**
   ```c
   static void
   tcp_parse_options(struct tcphdr *th, int hlen, struct tcp_options *opt)
   {
     opt->has_mss = 0;
     opt->has_wscale = 0;
     uint8 *p = (uint8 *)th + sizeof(struct tcphdr);
     uint8 *end = (uint8 *)th + hlen;

     while (p < end) {
       uint8 kind = *p++;
       if (kind == TCPOPT_EOL) break;
       if (kind == TCPOPT_NOP) continue;
       if (p >= end) break;
       uint8 len = *p++;
       if (len < 2 || p + (len - 2) > end) break;
       if (kind == TCPOPT_MSS && len == 4) {
         opt->mss = ((uint16)p[0] << 8) | (uint16)p[1];
         opt->has_mss = 1;
       } else if (kind == TCPOPT_WSCALE && len == 3) {
         opt->wscale = p[0];
         opt->has_wscale = 1;
       }
       p += (len - 2);
     }
   }
   ```

2. **Thương lượng Window Scale trong `tcp_output.c`:**
   - Khi gửi segment SYN hoặc SYN-ACK (`flags & TCP_FLAG_SYN`):
     * Gắn 8 byte options: MSS 1400 (Kind 2, Len 4) + NOP (Kind 1) + WSCALE (Kind 3, Len 3, Shift 2).
     * Tổng chiều dài TCP Header: $20 + 8 = 28$ bytes $\rightarrow$ `th->off = (28 / 4) << 4 = 0x70`.
     * Theo RFC 7323: Trong gói SYN/SYN-ACK, trường `win` **không được scale** (`th->win = htons(pcb->rcv_wnd)`).
   - Trong các segment dữ liệu hoặc pure ACK thông thường:
     * Cửa sổ quảng bá được dịch phải theo scale factor đã thống nhất:
       ```c
       uint32 scaled_win = pcb->rcv_wnd >> pcb->rcv_wscale;
       if (scaled_win > 65535) scaled_win = 65535;
       th->win = htons((uint16)scaled_win);
       ```

3. **Tính toán Send Window hiệu dụng trong `tcp_handle_ack()`:**
   - Khi nhận ACK từ đối tác:
     ```c
     uint32 eff_win = (uint32)win;
     if (pcb->wscale_ok)
       eff_win = eff_win << pcb->snd_wscale;
     pcb->snd_wnd = eff_win;
     ```
   - Nhờ đó, nếu client (như Linux / Google Chrome) gửi `win = 502` kèm hệ số scale 7 ($2^7 = 128$), `snd_wnd` thực tế đạt $502 \times 128 = 64,256$ bytes (thay vì bị bóp nghẹt ở mức 502 bytes như trước).

---

## 4. Kết Quả Kiểm Thử Thực Tế

### 4.1. Bộ Test Tự Động Máy Trạng Thái FSM (`tests/run_tests.py`)
Chạy bộ kiểm thử tự động toàn diện các tình huống bắt tay, đóng kết nối, rớt gói và abort:
```bash
python3 tests/run_tests.py
```
**Kết quả ghi nhận:**
```text
test_3way_handshake          ... OK
test_closed_port_rst         ... OK
test_half_close              ... OK
test_fragmented_stream       ... OK
test_largefile_1mb           ... OK
test_client_abort            ... OK

All 6 tests passed.
```
$\rightarrow$ **Vượt qua 100% kịch bản kiểm thử nền tảng**, không có bất kỳ xung đột hay hồi quy mã nguồn nào.

---

### 4.2. Kiểm Thử Chịu Tải 8 Kết Nối Đồng Thời (`tests/concurrent_stress.py --conns 8 --rounds 1`)
Trước khi triển khai, kịch bản này luôn bị treo 3/8 kết nối do tràn backlog và kết nối bị bỏ rơi. Sau khi nâng cấp:
```bash
python3 tests/concurrent_stress.py --conns 8 --rounds 1
```
**Kết quả thực tế ghi nhận:**
```text
Dot 1: 8 ket noi dong thoi (loss 0%)
  dot 1 / conn 1 largefile.bin -> 200 1048576 1.126398 OK
  dot 1 / conn 2 README        -> 200 6781 1.141267 OK
  dot 1 / conn 3 largefile.bin -> 200 1048576 2.311668 OK
  dot 1 / conn 4 README        -> 200 6781 1.148180 OK
  dot 1 / conn 5 largefile.bin -> 200 1048576 4.025050 OK
  dot 1 / conn 6 README        -> 200 6781 2.331738 OK
  dot 1 / conn 7 largefile.bin -> 200 1048576 5.455167 OK
  dot 1 / conn 8 README        -> 200 6781 5.466865 OK

=== KET QUA ===
Request thanh cong: 8/8 (5.5 s)
Pool mbuf sau dot cuoi: free=480/512 (mong doi 480 = NMBUF - rx_ring), max_burst_used=8
min_free thap nhat quan sat: 472
  Low watermark     : 0 dong log
  Recovered         : 0 dong log
  Pool empty        : 0 dong log
  RX drop (no mbuf) : 0 dong log
  RX overrun        : 0 dong log
  TX ring full      : 0 dong log
PASS
```
$\rightarrow$ **Hoàn thành 8/8 kết nối xuất sắc trong 5.5 giây (tỷ lệ thành công 100%)**. Đã xóa bỏ hoàn toàn lỗi treo timeout 60 giây trước đây!

---

### 4.3. Kiểm Thử Tải Cực Hạn Đa Đợt (`tests/concurrent_stress.py --conns 8 --rounds 3`)
Kiểm tra khả năng phục hồi liên tục và kiểm toán rò rỉ bộ nhớ qua 3 đợt tải dồn dập (tổng cộng 24 kết nối):
```bash
python3 tests/concurrent_stress.py --conns 8 --rounds 3
```
**Kết quả thực tế ghi nhận:**
```text
Dot 1: 8 ket noi dong thoi (loss 0%)
  dot 1 / conn 1 largefile.bin -> 200 1048576 1.820659 OK
  dot 1 / conn 2 README        -> 200 6781 5.555930 OK
  dot 1 / conn 3 largefile.bin -> 200 1048576 5.541597 OK
  dot 1 / conn 4 README        -> 200 6781 5.570239 OK
  dot 1 / conn 5 largefile.bin -> 200 1048576 3.737707 OK
  dot 1 / conn 6 README        -> 200 6781 1.837368 OK
  dot 1 / conn 7 largefile.bin -> 200 1048576 6.890392 OK
  dot 1 / conn 8 README        -> 200 6781 5.575760 OK
Dot 2: 8 ket noi dong thoi (loss 0%)
  dot 2 / conn 1 largefile.bin -> 200 1048576 3.227342 OK
  dot 2 / conn 2 README        -> 200 6781 5.852336 OK
  dot 2 / conn 3 largefile.bin -> 200 1048576 1.451144 OK
  dot 2 / conn 4 README        -> 200 6781 4.643905 OK
  dot 2 / conn 5 largefile.bin -> 200 1048576 5.842198 OK
  dot 2 / conn 6 README        -> 200 6781 4.639267 OK
  dot 2 / conn 7 largefile.bin -> 200 1048576 4.631953 OK
  dot 2 / conn 8 README        -> 200 6781 5.858067 OK
Dot 3: 8 ket noi dong thoi (loss 0%)
  dot 3 / conn 1 largefile.bin -> 200 1048576 3.589303 OK
  dot 3 / conn 2 README        -> 200 6781 1.737273 OK
  dot 3 / conn 3 largefile.bin -> 200 1048576 6.185706 OK
  dot 3 / conn 4 README        -> 200 6781 6.195524 OK
  dot 3 / conn 5 largefile.bin -> 200 1048576 1.716436 OK
  dot 3 / conn 6 README        -> 200 6781 3.614311 OK
  dot 3 / conn 7 largefile.bin -> 200 1048576 5.100977 OK
  dot 3 / conn 8 README        -> 200 6781 6.202091 OK

=== KET QUA ===
Request thanh cong: 24/24 (19.0 s)
Pool mbuf sau dot cuoi: free=480/512 (mong doi 480 = NMBUF - rx_ring), max_burst_used=8
min_free thap nhat quan sat: 472
  Low watermark     : 0 dong log
  Recovered         : 0 dong log
  Pool empty        : 0 dong log
  RX drop (no mbuf) : 0 dong log
  RX overrun        : 0 dong log
  TX ring full      : 0 dong log
PASS
```
$\rightarrow$ **Hoàn thành 24/24 requests đạt 100%**. Bộ đệm `mbuf_free_count` trở về đúng mức nền tĩnh `480/512` sau đợt tải cuối cùng, khẳng định không rò rỉ bất kỳ 1 mbuf nào.

---

### 4.4. Kiểm Thử Môi Trường Mất Gói (`--loss 5%`)
```bash
python3 tests/concurrent_stress.py --conns 5 --rounds 1 --loss 5
```
**Kết quả thực tế ghi nhận:**
```text
Dot 1: 5 ket noi dong thoi (loss 5%)
  dot 1 / conn 1 largefile.bin -> 200 1048576 6.223988 OK
  dot 1 / conn 2 README        -> 200 6781 0.019040 OK
  dot 1 / conn 3 largefile.bin -> 200 1048576 4.052919 OK
  dot 1 / conn 4 README        -> 200 6781 0.131704 OK
  dot 1 / conn 5 largefile.bin -> 200 1048576 1.678556 OK

=== KET QUA ===
Request thanh cong: 5/5 (6.2 s)
Pool mbuf sau dot cuoi: free=480/512, max_burst_used=34, min_free=446
PASS
```
$\rightarrow$ Động cơ TCP Reno + NewReno kích hoạt Fast Retransmit và Partial ACK chính xác khi có mất gói, kết hợp cùng SYN/Accept queue giúp toàn bộ 5 kết nối hoàn thành tốt đẹp.

---

## 5. Bảng Tổng Kết So Sánh Hiệu Năng & Độ Ổn Định

| Chỉ số đánh giá | Trước khi tối ưu Tuần 4 | Sau khi hoàn thành Tuần 4 | Đánh giá cải tiến |
| :--- | :--- | :--- | :--- |
| **8 kết nối đồng thời (`--conns 8`)** | Treo 3/8 kết nối (Timeout 60s) | **8/8 thành công (5.5s)** | **Giải quyết triệt để lỗi treo**, tăng tỷ lệ thành công từ 62.5% lên **100%** |
| **Thứ tự phục vụ kết nối** | LIFO (Đảo lộn thứ tự) | **FIFO (Chuẩn công nghiệp)** | Giảm độ lệch thời gian chờ, triệt tiêu starvation |
| **Tùy chọn TCP Options** | Chỉ có TCP header cố định 20B | **Hỗ trợ MSS (1400B) & Window Scale (Shift 2)** | Tương thích tiêu chuẩn RFC 7323 với trình duyệt thật |
| **Kích thước socket table** | 16 sockets | **32 sockets** | Tăng gấp đôi dung lượng, khớp với pool PCB |
| **An toàn rò rỉ bộ nhớ** | Dễ rò rỉ PCB mồ côi | **0 rò rỉ (Pool hồi phục 480/512)** | Đã được kiểm chứng tự động qua audit log |

---

## 6. Phối Hợp & Giao Thoa Nhóm (TV1 & TV3)

1. **Phối hợp với TV1 (Nguyễn Vũ Hiệp):**
   - Tận dụng hạ tầng card mạng E1000 nâng cấp 32 descriptor và bộ đếm `mbuf_free_count` cùng cơ chế kiểm toán `[mbuf] idle` để xác nhận tầng TCP giải phóng trọn vẹn mọi segment sau các đợt tải đa luồng.
2. **Chuẩn bị sẵn sàng cho TV3 (Lê Anh Đức):**
   - Kiến trúc Accept Queue FIFO đã được thiết kế đồng bộ với `s->lock` và cơ chế `sleep_prepare(s)` / `wakeup(ls)`. Khi TV3 triển khai Pre-fork Worker Pool trong `user/httpd.c` (4–8 tiến trình con cùng gọi `accept()`), tầng socket kernel sẽ tự động điều phối các worker tranh chấp nhận kết nối một cách an toàn, không xảy ra xung đột dữ liệu hay mất kết nối.
3. **Sửa lỗi công cụ hỗ trợ (`scripts/simulator.py`):**
   - Đã sửa lỗi `TypeError: abspath() takes 1 positional argument but 2 were given` tại dòng 13 của script mô phỏng, giúp việc kiểm thử tự động của TV3 diễn ra thuận lợi.

---

## 7. Danh Mục Tệp Mã Nguồn Tác Động

| Tệp | Vị trí thay đổi chính | Mô tả chi tiết kỹ thuật |
| :--- | :--- | :--- |
| `kernel/net.h` | `struct tcp_pcb`, `struct socket`, `#define TCPOPT_*` | Khai báo macro TCP Options, thêm `snd_wscale`, `rcv_wscale`, `wscale_ok`, `syn_next` vào PCB; thêm `backlog_tail`, `syn_q`, `syn_cnt`, `syn_max` vào socket |
| `kernel/sock.c` | `MAX_SOCKETS`, `sock_alloc`, `sock_close`, `sock_listen`, `sock_accept` | Nâng `MAX_SOCKETS` lên 32; quản lý giải phóng cả SYN/Accept queue khi đóng socket; thiết lập hàng đợi Accept FIFO qua `backlog_tail` |
| `kernel/tcp.c` | `tcp_pcb_alloc`, `tcp_pcb_free`, `tcp_timer_process` | Khởi tạo các trường Window Scale và liên kết SYN queue; đảm bảo gỡ PCB khỏi `syn_q` an toàn khi PCB bị free hoặc timeout |
| `kernel/tcp_input.c` | `tcp_parse_options`, `tcp_handle_ack`, `tcp_input` | Cài đặt hàm phân tích TCP options; tính `snd_wnd` theo hệ số scale; chống tạo orphaned PCB khi backlog đầy bằng cách drop ACK hoãn lại; hỗ trợ dữ liệu HTTP đi kèm gói bước 3 |
| `kernel/tcp_output.c` | `tcp_output` | Đóng gói TCP header 28 byte kèm MSS 1400 và Window Scale option khi gửi segment SYN/SYN-ACK; scale cửa sổ quảng bá theo RFC 7323 |
| `scripts/simulator.py`| `__init__` | Sửa lỗi cú pháp `os.path.abspath(os.path.join(...))` |
| `doc/week_4/Phùng Huy Hoàng - 20233414.md` | Toàn bộ tệp | Báo cáo chi tiết toàn diện tiến trình công việc Tuần 4 của Thành viên 2 |
