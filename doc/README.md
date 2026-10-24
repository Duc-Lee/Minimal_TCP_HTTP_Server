# HỒ SƠ TIẾN TRÌNH THỰC HIỆN DỰ ÁN THEO TUẦN (WEEKLY PROGRESS LOG)
## Dự Án: Minimal TCP/IP Stack & HTTP Server trên xv6-riscv (64-bit)

Thư mục này ghi lại toàn bộ tiến trình triển khai kỹ thuật, báo cáo công việc và kết quả thực nghiệm chi tiết của từng thành viên trong nhóm qua từng tuần từ **Tuần 1 đến Tuần 6**.

---

## Danh Sách Thành Viên & Phân Nhiệm

1. **Phùng Huy Hoàng - MSSV: 20233414**
   - **Vai trò:** Thành viên 1 (TV1) - Kỹ sư Hạ tầng Mạng, Driver E1000 & Quản lý Bộ nhớ Mbuf.
2. **Nguyễn Vũ Hiệp - MSSV: 20233392**
   - **Vai trò:** Thành viên 2 (TV2) - Kỹ sư Tầng Giao vận TCP, Máy trạng thái FSM & Thuật toán Tắc nghẽn.
3. **Lê Anh Đức - MSSV: 20233324**
   - **Vai trò:** Thành viên 3 (TV3) - Kỹ sư Tầng Ứng dụng, BSD Socket VFS, HTTP Server & Trực quan hóa.

---

## Mục Lục Báo Cáo Tiến Trình Chi Tiết

| Tuần | Chủ đề trọng tâm | Báo cáo chi tiết từng thành viên |
| :---: | :--- | :--- |
| **Tuần 1** | Khảo sát mã nguồn, vá lỗi Deadlock Mbuf, Chuẩn hóa RTO Timer 10 Hz & File 1 MB thật trên đĩa | - [Phùng Huy Hoàng - 20233414](week_1/Phùng%20Huy%20Hoàng%20-%2020233414.md) *(Báo cáo Giai đoạn 2)*<br>- [Nguyễn Vũ Hiệp - 20233392](week_1/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md)<br>- [Lê Anh Đức - 20233324](week_1/Lê%20Anh%20Đức%20-%2020233324.md) *(Báo cáo Giai đoạn 1)* |
| **Tuần 2** | Hàng đợi gói lệch thứ tự (`ooo_queue`), RFC 793 RST, Syscall Setloss & State Tests | - [Phùng Huy Hoàng - 20233414](week_2/Phùng%20Huy%20Hoàng%20-%2020233414.md)<br>- [Nguyễn Vũ Hiệp - 20233392](week_2/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md)<br>- [Lê Anh Đức - 20233324](week_2/Lê%20Anh%20Đức%20-%2020233324.md) |
| **Tuần 3** | Kiểm soát tắc nghẽn TCP Tahoe & Reno (RFC 5681), Đo RTT chuẩn Karn & Cửa sổ trượt `cwnd` | - [Phùng Huy Hoàng - 20233414](week_3/Phùng%20Huy%20Hoàng%20-%2020233414.md)<br>- [Nguyễn Vũ Hiệp - 20233392](week_3/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md)<br>- [Lê Anh Đức - 20233324](week_3/Lê%20Anh%20Đức%20-%2020233324.md) |
| **Tuần 4** | Web Server Pre-forked Multi-worker Pool, HTTP/1.1 Keep-Alive & Tương thích trình duyệt thật | - [Phùng Huy Hoàng - 20233414](week_4/Phùng%20Huy%20Hoàng%20-%2020233414.md)<br>- [Nguyễn Vũ Hiệp - 20233392](week_4/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md)<br>- [Lê Anh Đức - 20233324](week_4/Lê%20Anh%20Đức%20-%2020233324.md) |
| **Tuần 5** | Bộ tính năng đột phá: Zero-Copy `sys_sendfile` (đo `rdcycle`), Google BBR & Live Dashboard `tcptrace` | - [Phùng Huy Hoàng - 20233414](week_5/Phùng%20Huy%20Hoàng%20-%2020233414.md)<br>- [Nguyễn Vũ Hiệp - 20233392](week_5/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md)<br>- [Lê Anh Đức - 20233324](week_5/Lê%20Anh%20Đức%20-%2020233324.md) |
| **Tuần 6** | Stress test chịu tải nặng (50 clients), Khử rò rỉ bộ nhớ (0 leak), 4 Script Demo 1-Click & Báo cáo | - [Phùng Huy Hoàng - 20233414](week_6/Phùng%20Huy%20Hoàng%20-%2020233414.md)<br>- [Nguyễn Vũ Hiệp - 20233392](week_6/Nguyễn%20Vũ%20Hiệp%20-%2020233392.md)<br>- [Lê Anh Đức - 20233324](week_6/Lê%20Anh%20Đức%20-%2020233324.md) |
