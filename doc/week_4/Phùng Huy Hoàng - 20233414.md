# TIẾN TRÌNH TUẦN 4 - PHÙNG HUY HOÀNG (MSSV: 20233414)
## Báo Cáo Triển Khai Tuần 4: Tối Ưu Hóa Ring Descriptor E1000 & Quản Lý Mbuf Đa Kết Nối

* **Người thực hiện:** Phùng Huy Hoàng - MSSV: 20233414
* **Tuần:** Tuần 4 (Week 4)
* **Vai trò:** Thành viên 1 (TV1) - Hạ tầng Mạng, Bộ nhớ Mbuf & Driver E1000
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

## 1. Mục Tiêu Nhiệm Vụ Trong Tuần 4

1. **Gia cố Driver Intel E1000 chịu tải đa kết nối**:
   - Khi TV3 nâng cấp Web Server lên mô hình Multi-worker Pool (4-8 tiến trình đồng thời), lưu lượng gói tin phát sinh tăng đột biến. Cần tối ưu hóa vòng quét Ring Buffer của card mạng để tránh tràn hàng đợi TX/RX.
2. **Quản lý an toàn ngưỡng bộ nhớ Mbuf Pool**:
   - Theo dõi số lượng `mbuf` tự do (`mbuf_free_count`), thiết lập ngưỡng cảnh báo Low Watermark (dưới 10% dung lượng đệm) để điều tiết tốc độ nhận gói tin.

---

## 2. Chi Tiết Triển Khai Kỹ Thuật

### 2.1. Tối Ưu Hóa TX Ring Descriptor (`kernel/e1000.c`)
* Ban đầu, driver chỉ giải phóng TX descriptor khi vòng lặp truyền gói quay vòng lại. Khi có nhiều worker cùng gửi dữ liệu dung lượng lớn, TX Ring (16 descriptors) có nguy cơ bị đầy nhanh chóng.
* **Cải tiến:**
  - Triển khai hàm dọn dẹp chủ động `e1000_clean_tx()`:
    ```c
    void e1000_clean_tx(void) {
      // Quét các descriptor đã hoàn thành truyền (bit E1000_TXD_STAT_DD được bật bởi phần cứng)
      while (tx_clean_idx != tx_tail_idx) {
        if (!(tx_ring[tx_clean_idx].status & E1000_TXD_STAT_DD))
          break;
        if (tx_mbufs[tx_clean_idx]) {
          mbuffree(tx_mbufs[tx_clean_idx]);
          tx_mbufs[tx_clean_idx] = 0;
        }
        tx_clean_idx = (tx_clean_idx + 1) % TX_RING_SIZE;
      }
    }
    ```
  - Gọi `e1000_clean_tx()` định kỳ trong ngắt mạng và trước mỗi lần phát gói mới, giải phóng ngay `mbuf` đã gửi xong về pool.

### 2.2. Kiểm Soát Dung Lượng Mbuf Pool (`kernel/mbuf.c`)
* Mở rộng kích thước pool `mbuf` lên mức an toàn (128 – 256 buffers).
* Cơ chế cảnh báo sớm: Nếu số `mbuf` rảnh rỗi tụt xuống dưới 12 buffers, kernel tạm thời không phân bổ thêm mbuf mới cho các kết nối mở rộng mà ưu tiên phục vụ giải phóng các kết nối đang truyền tải dở dang.

---

## 3. Kết Quả Đo Đạc & Nghiệm Thu Tuần 4

1. **Độ ổn định khi chịu tải đồng thời:**
   - Khi chạy kiểm thử bắn đồng thời 8 kết nối tải file từ Host: Card mạng E1000 hoạt động trơn tru, không có gói tin nào bị drop tại tầng NIC do tràn Ring Buffer.
2. **Khôi phục bộ đệm:**
   - Các `mbuf` được dọn dẹp và thu hồi tức thì ngay khi card mạng truyền xong byte cuối cùng.
