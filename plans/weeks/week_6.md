# KẾ HOẠCH PHỐI HỢP TUẦN 6 (WEEK 6 PLAN)
## Chủ đề: Stress Test Tải Nặng, Khử Rò Rỉ Bộ Nhớ (0 Leak), Script Demo 1-Click & Hoàn Thiện

---

## 1. Trọng Tâm & Mục Tiêu Kỹ Thuật

* **Bối cảnh:** Đây là tuần cuối cùng trước ngày bảo vệ đồ án. Trọng tâm của tuần là kiểm tra độ bền bỉ trong các tình huống cực đoan nhất, chứng minh hệ thống không bị rò rỉ bộ nhớ (Zero Memory Leak), tự động hóa toàn bộ quy trình demo bằng các script 1-Click và hoàn thiện hồ sơ báo cáo.
* **Mục tiêu Tuần 6:**
  1. Thực hiện Stress Test chịu tải nặng với 50 kết nối đồng thời từ ApacheBench.
  2. Xác minh tỷ lệ thất thoát bộ đệm `mbuf` bằng 0 tuyệt đối sau hàng nghìn gói tin truyền nhận.
  3. Đóng gói 4 Script Demo 1-Click phục vụ diễn tập và trình chiếu trước Hội đồng.
  4. Hoàn thiện tài liệu kỹ thuật tổng thể và slide thuyết trình.

---

## 2. Phân Công Chi Tiết & Giao Thoa Giữa Các Thành Viên

| Thành viên | Trách nhiệm chính trong Tuần 6 | Files tác động | Đầu ra (Deliverables) |
| :--- | :--- | :--- | :--- |
| **TV1** | - Kiểm thử độ bền với tỉ lệ mất gói khắc nghiệt (20%, 30%).<br>- Viết hàm kiểm toán bộ nhớ kernel: Xác minh `mbuf_free_count == MBUF_TOTAL_COUNT` sau khi đóng toàn bộ kết nối.<br>- Kiểm tra giải phóng sạch sẽ các bản ghi ARP và hàng đợi truyền lại. | `kernel/mbuf.c`<br>`kernel/arp.c`<br>`kernel/e1000.c` | Bằng chứng kiểm toán chứng minh hệ thống đạt chuẩn Zero Memory Leak (0% rò rỉ RAM). |
| **TV2** | - Kiểm tra các tình huống kết nối biên: Client ngắt giữa chừng, Client gửi FIN dở dang (Half-close), Client gửi gói rác.<br>- Xác minh cơ chế thu hồi PCB sau 2 MSL trong trạng thái `TIME_WAIT`. | `kernel/tcp.c`<br>`kernel/tcp_input.c` | Tầng TCP sạch bóng zombie connection, không bị treo tài nguyên socket khi client ngắt bất thường. |
| **TV3** | - Viết 4 script tự động hóa demo 1-Click trong thư mục `scripts/`.<br>- Chạy bài Stress Test tải nặng với 50 client đồng thời.<br>- Biên soạn Slide báo cáo thuyết trình và kịch bản diễn tập Demo 2 màn hình 10 phút. | `scripts/*.sh`<br>`doc/final_report.md` | Bộ demo 1-click hoàn chỉnh; slide thuyết trình và số liệu benchmark thực tế đầy đủ. |

---

## 3. Bộ 4 Kịch Bản Demo 1-Click (`scripts/`)

1. **`scripts/demo1_core_1mb_loss10.sh` (Demo Yêu Cầu Cốt Lõi):**
   - Tự động gọi xv6 cấu hình `setloss 10`.
   - Chạy lệnh `curl` tải file 1 MB `largefile.bin` từ web server.
   - Tính toán và so khớp mã SHA-256 trên màn hình: Báo xanh nếu khớp 100%.

2. **`scripts/demo2_reno_vs_bbr.sh` (Demo So Sánh Tắc Nghẽn):**
   - Chạy tải file 1 MB với thuật toán Reno -> In thời gian và tốc độ.
   - Chuyển sang thuật toán BBR -> Chạy lại tải file -> In bảng so sánh trực quan cho thấy BBR vượt trội về tốc độ.

3. **`scripts/demo3_sendfile_benchmark.sh` (Demo Zero-Copy Sendfile):**
   - Chạy tải file bằng phương pháp `read/write` truyền thống -> In số triệu chu kỳ CPU tiêu tốn.
   - Chạy tải file bằng Zero-Copy `sys_sendfile` -> In số chu kỳ CPU và tỷ lệ % tài nguyên CPU đã tiết kiệm.

4. **`scripts/demo4_ab_concurrency.sh` (Demo Đa Kết Nối Đồng Thời):**
   - Sử dụng ApacheBench bắn 50 requests đồng thời: `ab -n 200 -c 50 http://127.0.0.1:20080/index.html`.
   - In bảng kết quả độ trễ và tỷ lệ thành công 100%.

---

## 4. Tiêu Chí Nghiệm Thu Cuối Tuần 6 (Final Definition of Done)

1. Toàn bộ 4 kịch bản Demo 1-Click thực thi trơn tru, không có bất kỳ lỗi phát sinh hay sự cố sập hệ thống nào.
2. Kiểm toán bộ nhớ sau bài kiểm tra stress test: biến đếm `mbuf_free_count` hoàn toàn nguyên vẹn 100% dung lượng ban đầu.
3. Diễn tập bài thuyết trình 10 phút hoàn chỉnh (2 màn hình: 1 màn hình console xv6 chạy `tcptrace` và 1 màn hình Host chạy script demo).
4. Hồ sơ mã nguồn sạch sẽ, không file rác, toàn bộ tài liệu được đồng bộ hóa lên GitHub.
