# TIẾN TRÌNH TUẦN 5 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai Tuần 5: Xây Dựng Hạ Tầng Đo Nhịp Xung Phần Cứng RISC-V Cho BBR

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 5 (Week 5)
* **Vai trò:** Thành viên 1 (TV1) - Hạ tầng Mạng, Bộ nhớ Mbuf & Driver E1000
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 5

1. **Xây dựng Giao tiếp Đồng hồ Phần cứng RISC-V Độ Chính Xác Cao**:
   - Thuật toán kiểm soát tắc nghẽn Google BBR đòi hỏi đo độ trễ gói tin và ước lượng băng thông ở mức phân giải microsecond. Đồng hồ tick 100ms của hệ điều hành là quá thô sơ.
   - Hiện thực các hàm đọc trực tiếp thanh ghi nhịp xung thời gian thực của kiến trúc RISC-V: `r_time()` và thanh ghi đếm chu kỳ lệnh `rdcycle()`.
2. **Cung cấp API Thời gian thực & Benchmark CPU**:
   - Cung cấp API `kernel_time_us()` cho TV2 phục vụ tính toán BBR `min_rtt`.
   - Cung cấp hàm đọc chu kỳ CPU cho TV3 phục vụ đo lường hiệu năng Zero-Copy `sys_sendfile`.

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Đọc Thanh Ghi Phần Cứng RISC-V Trong `kernel/riscv.h`
* Thêm hàm đọc thanh ghi chu kỳ lệnh (Hardware Cycle Counter):
  ```c
  static inline uint64 rdcycle(void) {
    uint64 x;
    asm volatile("rdcycle %0" : "=r" (x));
    return x;
  }
  ```
* Thêm hàm đọc thanh ghi thời gian thực (Real-time Clock):
  ```c
  static inline uint64 r_time(void) {
    uint64 x;
    asm volatile("csrr %0, time" : "=r" (x));
    return x;
  }
  ```

### 2.2. Chuyển Đổi Sang Microsecond Chính Xác (`kernel/tcp_timer.c`)
* Với xung nhịp phần cứng QEMU RISC-V là 10 MHz ($10,000,000\text{ ticks/giây}$), mỗi microsecond ($1\,\mu\text{s}$) tương ứng với đúng 10 ticks đồng hồ:
  ```c
  uint64 get_time_us(void) {
    return r_time() / 10;
  }
  ```
* Nhờ hàm này, TV2 có thể ghi lại nhãn thời gian phát gói `tx_timestamp_us` và đo RTT thực tế với độ phân giải siêu mịn khi nhận được ACK.

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 5

1. **Độ phân giải thời gian:**
   - Đạt độ chính xác microsecond ($1\,\mu\text{s}$), đáp ứng 100% yêu cầu khắt khe của giải thuật Google BBR.
2. **Hỗ trợ đo lường CPU:**
   - Thanh ghi `rdcycle` cung cấp chỉ số chu kỳ CPU không có độ trễ, phục vụ trực tiếp cho bài toán chứng minh hiệu quả giảm tải CPU của Zero-Copy `sys_sendfile`.
