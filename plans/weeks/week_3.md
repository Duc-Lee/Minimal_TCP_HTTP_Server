# KẾ HOẠCH PHỐI HỢP TUẦN 3 (WEEK 3 PLAN)
## Chủ đề: Kiểm Soát Tắc Nghẽn Toàn Diện: TCP Tahoe & TCP Reno (RFC 5681)

---

## 1. Trọng Tâm & Mục Tiêu Kỹ Thuật

* **Bối cảnh:** Nếu chỉ dựa vào cửa sổ nhận tĩnh (`snd_wnd`), bên gửi sẽ đẩy dữ liệu ồ ạt làm tràn hàng đợi của mạng. Khi xảy ra mất gói, cơ chế đơn giản phải chờ hết hạn RTO mới truyền lại gây sụt giảm thông lượng nghiêm trọng.
* **Mục tiêu Tuần 3:**
  1. Hiện thực thuật toán kiểm soát tắc nghẽn công nghiệp chuẩn **RFC 5681 (TCP Reno)**.
  2. Kích hoạt cơ chế **Fast Retransmit** khi nhận 3 Duplicate ACKs (không cần chờ RTO timeout) và **Fast Recovery**.
  3. Áp dụng **Quy tắc Karn (Karn's Algorithm)** để loại bỏ sai số khi tính toán RTT.
  4. Xây dựng cơ chế trích xuất dữ liệu biến thiên của cửa sổ tắc nghẽn `cwnd`.

---

## 2. Phân Công Chi Tiết & Giao Thoa Giữa Các Thành Viên

| Thành viên | Trách nhiệm chính trong Tuần 3 | Files tác động | Đầu ra (Deliverables) |
| :--- | :--- | :--- | :--- |
| **TV1** | - Áp dụng Quy tắc Karn trong `kernel/tcp_timer.c`: Đánh dấu segment truyền lại và không cập nhật RTT cho các gói này.<br>- Chuẩn hóa công thức Jacobson/Karels (RFC 6298) bằng số nguyên cố định (Fixed-point arithmetic).<br>- Đặt ngưỡng tối đa `MAX_BACKOFF = 12` và giải phóng kết nối hỏng. | `kernel/tcp_timer.c`<br>`kernel/tcp.c`<br>`kernel/net.h` | Bộ đếm thời gian RTO hội tụ chính xác với độ trễ thực tế, không bị trôi do mất gói liên tục. |
| **TV2** | - Mở rộng cấu trúc `struct tcp_pcb`: thêm `cwnd`, `ssthresh`, `dupack_cnt`, `recover`.<br>- Hiện thực logic Reno trong `kernel/tcp_cc.c`: Slow Start, Congestion Avoidance, Fast Retransmit khi nhận 3 Dup-ACKs, Fast Recovery.<br>- Áp dụng điều kiện gửi vào `kernel/tcp_output.c`: `snd_nxt - snd_una < min(snd_wnd, cwnd)`. | `kernel/tcp_cc.c`<br>`kernel/tcp_input.c`<br>`kernel/tcp_output.c`<br>`kernel/net.h` | Tầng TCP tự động điều tiết tốc độ phát gói; Fast Retransmit truyền lại gói thiếu ngay lập tức. |
| **TV3** | - Nâng cấp Socket Ring Buffer (`kernel/ringbuf.c`) đảm bảo an toàn truy cập đa luồng.<br>- Bổ sung hàm dump số liệu thống kê TCP từ kernel.<br>- Viết script Python trên Host vẽ đồ thị biến thiên của `cwnd` và `ssthresh` theo thời gian. | `kernel/ringbuf.c`<br>`kernel/sysnet.c`<br>`scripts/plot_cwnd.py` | Đồ thị răng cưa đặc trưng của TCP Reno được vẽ trực quan từ dữ liệu thực tế. |

---

## 3. Chi Tiết Thuật Toán TCP Reno Được Hiện Thực Trong `kernel/tcp_cc.c`

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
                     │  ssthresh = cwnd / 2  │
                     │  cwnd = ssthresh + 3  │
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

---

## 4. Tiêu Chí Nghiệm Thu Cuối Tuần 3 (Definition of Done)

1. Khi mất gói ngẫu nhiên 10%, Wireshark ghi nhận các gói tin bị mất được truyền lại ngay lập tức sau 3 gói Duplicate ACK mà không cần phải chờ hết hạn RTO (200ms).
2. Tải file 1 MB có TCP Reno nhanh hơn rõ rệt so với cơ chế chỉ dùng RTO timeout thông thường.
3. Script `plot_cwnd.py` sinh ra biểu đồ `cwnd` dạng đường răng cưa chuẩn mực của mô hình AIMD (Additive Increase Multiplicative Decrease).
