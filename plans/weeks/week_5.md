# KẾ HOẠCH PHỐI HỢP TUẦN 5 (WEEK 5 PLAN)
## Chủ đề: Bộ Tính Năng Đột Phá ("Wow" Factor): Zero-Copy sys_sendfile, Google BBR & Live Observability

---

## 1. Trọng Tâm & Mục Tiêu Kỹ Thuật

* **Bối cảnh:** Để đạt mức điểm Xuất sắc (Mức 3) trước Hội đồng chấm thi, dự án cần các tính năng mang tính đột phá về mặt kiến trúc hệ điều hành và khoa học máy tính: loại bỏ chi phí sao chép bộ nhớ (Zero-Copy), ứng dụng thuật toán kiểm soát tắc nghẽn hiện đại nhất (Google BBR), và công cụ giám sát trực quan thời gian thực (Live Dashboard).
* **Mục tiêu Tuần 5:**
  1. Hiện thực lời gọi hệ thống Zero-Copy `sys_sendfile` truyền trực tiếp từ đĩa sang mạng mà không qua không gian người dùng; đo lường số chu kỳ CPU tiết kiệm được bằng lệnh RISC-V `rdcycle`.
  2. Triển khai thuật toán **Google BBR Congestion Control** trong kernel xv6 dựa trên nhịp xung phần khám phá từ phần cứng.
  3. Xây dựng công cụ giám sát trực tiếp trên terminal xv6: lệnh `netstat` và dashboard `tcptrace` vẽ biểu đồ thanh ASCII.

---

## 2. Phân Công Chi Tiết & Giao Thoa Giữa Các Thành Viên

| Thành viên | Trách nhiệm chính trong Tuần 5 | Files tác động | Đầu ra (Deliverables) |
| :--- | :--- | :--- | :--- |
| **TV1** | - Xây dựng hàm đọc thanh ghi chu kỳ lệnh phần cứng RISC-V: `rdcycle()` và đồng hồ thời gian thực `r_time()`.<br>- Cung cấp API đo lường độ phân giải microsecond hỗ trợ bộ tính toán của BBR. | `kernel/riscv.h`<br>`kernel/tcp_timer.c` | Tầng phần cứng cung cấp chỉ số thời gian chính xác cao phục vụ đo RTT tối thiểu và chu kỳ CPU. |
| **TV2** | - Cài đặt thuật toán **Google BBR** trong `kernel/tcp_cc.c`: Máy trạng thái 4 pha (`STARTUP`, `DRAIN`, `PROBE_BW`, `PROBE_RTT`).<br>- Đo đạc cửa sổ trượt 10 giây `min_rtt` và ước lượng băng thông tối đa `max_bw`.<br>- Syscall `sys_set_cc(int algo)` chuyển đổi Reno / BBR. | `kernel/tcp_cc.c`<br>`kernel/sysnet.c`<br>`kernel/syscall.h`<br>`user/user.h` | Kernel hỗ trợ chuyển đổi linh hoạt giữa Reno và BBR; BBR duy trì thông lượng cao trong môi trường mất gói 10%. |
| **TV3** | - Cài đặt Zero-Copy `sys_sendfile` trong `kernel/sendfile.c`: Đọc từ Buffer Cache đĩa qua `bread()` sang `mbuf` trực tiếp.<br>- Đo đạc chu kỳ CPU bằng `rdcycle` trước và sau khi truyền.<br>- Hiện thực chương trình dòng lệnh `user/netstat.c`.<br>- Hiện thực Dashboard ASCII thời gian thực `user/tcptrace.c`. | `kernel/sendfile.c`<br>`kernel/sysnet.c`<br>`user/netstat.c`<br>`user/tcptrace.c`<br>`user/httpd.c` | `sys_sendfile` giảm tiêu thụ CPU; `tcptrace` hiển thị trực quan đồ thị thông số mạng trực tiếp trên màn hình console. |

---

## 3. So Sánh Kiến Trúc Truyền Dữ Liệu: Read/Write vs. Zero-Copy `sys_sendfile`

```
TRUYỀN TRUYỀN THỐNG (Read/Write - 4 lần Copy):
[Đĩa] ──(DMA)──> [Buffer Cache Kernel] ──(Copy 1)──> [Buffer User-space]
                                                               │
[NIC] <──(DMA)── [mbuf Kernel] <──(Copy 2)────────────────────┘

ZERO-COPY sys_sendfile (0 lần Copy qua User-space):
[Đĩa] ──(DMA)──> [Buffer Cache Kernel] ──(Copy nội bộ Kernel)──> [mbuf Kernel] ──(DMA)──> [NIC]
```

---

## 4. Tiêu Chí Nghiệm Thu Cuối Tuần 5 (Definition of Done)

1. **Hiệu năng Zero-Copy:**
   Chạy công cụ đo chu kỳ CPU: `sys_sendfile` giúp giảm ít nhất **25% - 40% số chu kỳ xung nhịp CPU** so với phương pháp `read()` kết hợp `write()` thông thường.
2. **Hiệu năng Google BBR:**
   Khi bật tỉ lệ rớt gói 10% bằng lệnh `setloss 10`, thuật toán Google BBR đạt tốc độ truyền tải nhanh gấp **1.5 lần đến 2 lần** so với TCP Reno do không bị hạ cửa sổ mù quáng khi mất gói.
3. **Trực quan hóa Live Observability:**
   Chạy chương trình `tcptrace` trên console xv6 trong khi client đang tải file: Các thanh ASCII bar chart nhảy số liên tục, cập nhật trực quan kích thước `cwnd`, `ssthresh` và `RTO` sau mỗi 500ms.
