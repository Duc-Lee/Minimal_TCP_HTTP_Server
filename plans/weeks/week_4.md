# KẾ HOẠCH PHỐI HỢP TUẦN 4 (WEEK 4 PLAN)
## Chủ đề: Chinh Phục Đa Kết Nối Đồng Thời (Worker Pool), HTTP/1.1 & Tương Thích Trình Duyệt Thật

---

## 1. Trọng Tâm & Mục Tiêu Kỹ Thuật

* **Bối cảnh:** Web server ban đầu chỉ có một tiến trình đơn (Single-process model). Nếu một client đang tải file lớn hoặc trình duyệt gửi kết nối giữ nguyên (Keep-Alive), toàn bộ các client khác sẽ bị nghẽn (Head-of-Line Blocking). Ngoài ra, trình duyệt web hiện đại (Chrome, Firefox) gửi header rất dài và tự động yêu cầu file `/favicon.ico`, dễ làm sập server sơ khai.
* **Mục tiêu Tuần 4:**
  1. Nâng cấp Web Server lên mô hình **Pre-forked Multi-Worker Pool** (4–8 tiến trình con chạy song song phục vụ độc lập).
  2. Hỗ trợ tiêu chuẩn **HTTP/1.1**: Header `Connection: keep-alive`, `Content-Length`, phân loại MIME types chính xác.
  3. Hoàn thiện tính tương thích với trình duyệt web thật trên máy tính Host.
  4. Hỗ trợ tùy chọn TCP Window Scale (RFC 7323) để mở rộng cửa sổ truyền nhận.

---

## 2. Phân Công Chi Tiết & Giao Thoa Giữa Các Thành Viên

| Thành viên | Trách nhiệm chính trong Tuần 4 | Files tác động | Đầu ra (Deliverables) |
| :--- | :--- | :--- | :--- |
| **TV1** | - Tối ưu hóa hàng đợi Ring Descriptor card mạng E1000 để chịu lưu lượng lớn từ nhiều tiến trình cùng lúc.<br>- Đánh giá ngưỡng bộ nhớ mbuf pool, bổ sung cảnh báo Low Watermark khi dung lượng đệm tụt thấp. | `kernel/e1000.c`<br>`kernel/mbuf.c` | Tầng mạng hoạt động ổn định dưới áp lực đa luồng, không bị tràn ring buffer dẫn đến drop gói tại NIC. |
| **TV2** | - Tách biệt rõ ràng giữa SYN Queue và Accept Queue trong socket PCB.<br>- Hỗ trợ phân tích tùy chọn **TCP Window Scale Option (RFC 7323)** trong TCP header khi bắt tay 3 bước với trình duyệt. | `kernel/tcp.c`<br>`kernel/tcp_input.c`<br>`kernel/net.h` | Cửa sổ nhận có khả năng mở rộng trên 64 KB; hàng đợi lắng nghe `backlog_q` không bị nghẽn khi có dồn ứ kết nối. |
| **TV3** | - Kiến trúc Pre-fork Worker Pool trong `user/httpd.c`: Tiến trình cha `fork()` ra 4-8 workers cùng cạnh tranh `accept()`.<br>- Tăng buffer đọc HTTP request lên 2048 bytes.<br>- Xử lý route `/favicon.ico` trả về `204 No Content`.<br>- Triển khai HTTP/1.1 Keep-Alive và tự động nhận dạng file types. | `user/httpd.c` | Web Server phục vụ đồng thời nhiều client; trình duyệt Chrome/Firefox truy cập mượt mà. |

---

## 3. Kiến Trúc Pre-Forked Worker Pool Của Web Server (`user/httpd.c`)

```
                      ┌─────────────────────────┐
                      │    Master Process       │
                      │  bind(80) & listen(16)  │
                      └────────────┬────────────┘
                                   │ fork() x 4
        ┌──────────────────────────┼──────────────────────────┐
        ▼                          ▼                          ▼
┌───────────────┐          ┌───────────────┐          ┌───────────────┐
│   Worker 1    │          │   Worker 2    │          │   Worker 3    │
│ accept(listen)│          │ accept(listen)│          │ accept(listen)│
│ Phục vụ Client│          │ Phục vụ Client│          │ Phục vụ Client│
└───────────────┘          └───────────────┘          └───────────────┘
```

---

## 4. Tiêu Chí Nghiệm Thu Cuối Tuần 4 (Definition of Done)

1. **Kiểm thử trên trình duyệt thật (Browser Test):**
   Mở trình duyệt Google Chrome hoặc Mozilla Firefox trên Host, truy cập `http://localhost:20080`:
   - Trang HTML hiển thị lập tức, không bị tình trạng xoay vòng chờ vô tận (hanging).
   - Tải file nhị phân lớn hoặc ảnh minh họa thành công trọn vẹn.
   - Không xuất hiện lỗi crash server do request header quá dài hoặc do thiếu favicon.

2. **Kiểm thử chịu tải đồng thời (ApacheBench Test):**
   Chạy công cụ đo tải từ máy Host:
   ```bash
   ab -n 200 -c 8 http://127.0.0.1:20080/index.html
   ```
   - Tỷ lệ hoàn thành: 100% (Failed requests = 0).
   - Phản hồi đồng thời từ nhiều worker không gây xung đột tài nguyên socket.
