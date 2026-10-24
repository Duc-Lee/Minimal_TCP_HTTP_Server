# TIẾN TRÌNH TUẦN 1 - NGUYỄN VŨ HIỆP (MSSV: 20233392)
## Báo Cáo Triển Khai Giai Đoạn 3: Nâng Cấp Hệ Thống Tệp Đĩa Thật (Doubly-Indirect Block), Web Server Đọc File 1 MB Thật & Toàn Vẹn Dữ Liệu

* **Người thực hiện:** Nguyễn Vũ Hiệp - MSSV: 20233392
* **Tuần:** Tuần 1 (Week 1)
* **Repository:** Minimal_TCP_HTTP_Server (xv6-riscv 64-bit)

---

Tài liệu này mô tả chi tiết toàn bộ quá trình khảo sát mã nguồn, phân tích kiến trúc hệ thống tệp, thiết kế giải pháp kỹ thuật, triển khai mã nguồn và đo đạc kết quả thực nghiệm trong **Giai đoạn 3 (Tuần 1 – Nâng cấp hệ thống tệp đĩa thật & Hoàn thiện nhiệm vụ Thành viên 3)** của dự án Minimal TCP & HTTP Server trên hệ điều hành **xv6-riscv** (64-bit).

---

## 1. Mục tiêu Giai đoạn 3

Giai đoạn 3 tập trung giải quyết bài toán lưu trữ và phân phối tệp tin dung lượng lớn thực tế trên nền tảng hệ điều hành giáo dục xv6:
1. **Phá vỡ giới hạn dung lượng tệp tin của xv6 chuẩn**: Cải tạo cấu trúc Inode từ Singly-Indirect sang **Doubly-Indirect Block**, nâng giới hạn kích thước tệp tin tối đa từ **~268 KB** lên **~65.8 MB**, đáp ứng lưu trữ các tệp đa phương tiện và dữ liệu 1 MB thật.
2. **Bảo toàn tính căn chỉnh bộ nhớ đĩa (Memory & Disk Layout Alignment)**: Đảm bảo kích thước cấu trúc `struct dinode` trên đĩa và `struct inode` trong RAM luôn là bội số/ước số chuẩn xác của `BSIZE` (1024 bytes), tránh hiện tượng tràn bộ đệm hoặc phá hỏng bảng Inode đĩa.
3. **Mở rộng dung lượng phân vùng đĩa ảo**: Nâng kích thước hệ thống tệp từ `FSSIZE = 2000` blocks lên `FSSIZE = 20000` blocks (~20 MB).
4. **Đồng bộ hóa công cụ định dạng đĩa `mkfs`**: Cập nhật logic ghi đĩa offline trong `mkfs/mkfs.c` hỗ trợ ánh xạ 2 tầng con trỏ, cho phép nạp tệp dung lượng lớn trực tiếp vào `fs.img` khi biên dịch.
5. **Chấm dứt hoàn toàn cơ chế Mock byte ảo**: Xóa bỏ nhánh sinh dữ liệu giả lập trong RAM tại Web Server `user/httpd.c`, chuyển sang mô hình đọc luồng dữ liệu thật từ đĩa qua các system call `open()` và `read()`.
6. **Đảm bảo tính toàn vẹn dữ liệu 100%**: Truyền tải thành công tệp `largefile.bin` 1 MB ($1,048,576\text{ bytes}$) qua mạng TCP/IP, mã băm SHA-256 sau khi tải về trên Host trùng khớp tuyệt đối với tệp gốc trên đĩa.

---

## 2. Chi Tiết Vấn Đề Kỹ Thuật, Nguyên Nhân Gốc Rễ & Giải Pháp Triển Khai

### 2.1. Giới hạn dung lượng tệp tin của Inode nguyên bản trong xv6
* **Triệu chứng & Giới hạn:**
  - Cấu trúc Inode nguyên bản của xv6 chỉ định nghĩa 12 con trỏ trực tiếp (`NDIRECT = 12`) và 1 con trỏ gián tiếp cấp 1 (`NINDIRECT = BSIZE / sizeof(uint) = 1024 / 4 = 256` blocks).
  - Dung lượng tệp tin tối đa có thể biểu diễn được là:
    $$\text{MAXFILE}_{\text{cũ}} = (12 + 256) \times 1024\text{ bytes} = 268\text{ KB}$$
  - Khi cố gắng ghi một tệp tin dung lượng 1 MB ($1,048,576\text{ bytes} \approx 1024\text{ blocks}$) vào hệ thống tệp, hàm `bmap()` ngay lập tức kích hoạt `panic("bmap: out of range")`.
* **Giải pháp kỹ thuật:**
  - Thay thế 1 con trỏ trực tiếp bằng **1 con trỏ gián tiếp kép (Doubly-Indirect Block Pointer)**.
  - Con trỏ này trỏ tới một block cấp 1 chứa 256 địa chỉ của các block cấp 2; mỗi block cấp 2 tiếp tục chứa 256 địa chỉ của các block dữ liệu.
  - Dung lượng tệp tin tối đa sau khi nâng cấp đạt:
    $$\text{MAXFILE}_{\text{mới}} = (11 + 256 + 256 \times 256) \times 1024\text{ bytes} = (11 + 256 + 65,536) \times 1024\text{ bytes} \approx 65.8\text{ MB}$$
  - Hoàn toàn vượt mức yêu cầu 1 MB và sẵn sàng cho các bài kiểm thử tải nặng trong tương lai.

---

### 2.2. Thiết kế căn chỉnh cấu trúc Inode trên đĩa (`kernel/fs.h` & `kernel/file.h`)
* **Nguy cơ lỗi tiềm ẩn:**
  - Kích thước `struct dinode` trong xv6 bắt buộc phải là ước số của `BSIZE = 1024` (tức là `BSIZE % sizeof(struct dinode) == 0`) để số lượng Inode trên mỗi block (`IPB`) luôn là số nguyên.
  - Nếu chỉ đơn thuần tăng kích thước mảng `addrs` mà không giảm `NDIRECT`, kích thước struct sẽ bị lẻ (ví dụ 68 bytes), dẫn đến phép chia `1024 % 68 = 4 != 0`, gây lỗi `Assertion failed` và làm hỏng toàn bộ bảng Inode trên đĩa.
* **Giải pháp kỹ thuật chuẩn xác:**
  - Giảm `NDIRECT` từ `12` xuống `11`.
  - Mảng `addrs` trong `struct dinode` có kích thước là `NDIRECT + 2 = 11 + 2 = 13` phần tử (gồm 11 direct, 1 single-indirect tại index 11, và 1 double-indirect tại index 12).
  - Kích thước `struct dinode`:
    $$\text{sizeof(dinode)} = 2\text{ (type)} + 2\text{ (major)} + 2\text{ (minor)} + 2\text{ (nlink)} + 4\text{ (size)} + 13 \times 4\text{ (addrs)} = 64\text{ bytes}$$
    $$1024 \pmod{64} = 0 \quad (\text{IPB} = 1024 / 64 = 16\text{ inodes/block})$$
  - Trong `kernel/fs.h`:
    ```c
    #define NDIRECT    11
    #define NINDIRECT  (BSIZE / sizeof(uint))
    #define NDINDIRECT (NINDIRECT * NINDIRECT)
    #define MAXFILE    (NDIRECT + NINDIRECT + NDINDIRECT)
    #define NLINK_MAX  32767

    struct dinode {
      short type;              // File type
      short major;             // Major device number (T_DEVICE only)
      short minor;             // Minor device number (T_DEVICE only)
      short nlink;             // Number of links to inode in file system
      uint size;               // Size of file (bytes)
      uint addrs[NDIRECT + 2]; // Data block addresses (11 direct + 1 single + 1 double)
    };
    ```
  - Đồng bộ `struct inode` trong bộ nhớ tại `kernel/file.h`:
    ```c
    struct inode {
      uint dev;
      uint inum;
      int ref;
      struct sleeplock lock;
      int valid;
      short type;
      short major;
      short minor;
      short nlink;
      uint size;
      uint addrs[NDIRECT + 2]; // Dong bo 13 phan tu
    };
    ```

---

### 2.3. Cấp phát khối 2 tầng trong `bmap()` và giải phóng đĩa đệ quy trong `itrunc()`
* **Triển khai hàm `bmap()` (`kernel/fs.c`):**
  - Khi chỉ số block ảo `bn` vượt quá vùng Single-indirect (`bn >= NINDIRECT`), thuật toán trừ đi `NINDIRECT` để tính offset tương đối trong vùng Doubly-indirect (`bn < NDINDIRECT`).
  - Ánh xạ 2 tầng:
    1. **Tầng 1:** Đọc/cấp phát khối mục lục cấp 1 tại `ip->addrs[NDIRECT + 1]`. Chỉ số mục lục cấp 1: `l1_idx = bn / NINDIRECT`.
    2. **Tầng 2:** Đọc/cấp phát khối mục lục cấp 2 tại địa chỉ lấy từ tầng 1. Chỉ số mục lục cấp 2: `l2_idx = bn % NINDIRECT`.
    3. Đọc/cấp phát khối dữ liệu thực tế tại `a[l2_idx]`.
  ```c
  // Double-indirect block
  if (bn < NDINDIRECT) {
    if ((addr = ip->addrs[NDIRECT + 1]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0) return 0;
      ip->addrs[NDIRECT + 1] = addr;
    }

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    uint l1_idx = bn / NINDIRECT;
    uint l2_idx = bn % NINDIRECT;

    if ((addr = a[l1_idx]) == 0) {
      addr = balloc(ip->dev);
      if (addr == 0) { brelse(bp); return 0; }
      a[l1_idx] = addr;
      log_write(bp);
    }
    brelse(bp);

    bp = bread(ip->dev, addr);
    a = (uint *)bp->data;
    if ((addr = a[l2_idx]) == 0) {
      addr = balloc(ip->dev);
      if (addr) {
        a[l2_idx] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }
  ```

* **Triển khai hàm `itrunc()` (`kernel/fs.c`):**
  - Đảm bảo khi file bị xóa hoặc truncate về 0, kernel giải phóng triệt để toàn bộ:
    1. Các data blocks được trỏ bởi các block cấp 2.
    2. Toàn bộ các block mục lục cấp 2.
    3. Block mục lục cấp 1 tại `ip->addrs[NDIRECT + 1]`.
  - Không để xảy ra rò rỉ sector đĩa (Disk Block Leak).
  ```c
  if (ip->addrs[NDIRECT + 1]) {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint *)bp->data;
    for (j = 0; j < NINDIRECT; j++) {
      if (a[j]) {
        struct buf *bp2 = bread(ip->dev, a[j]);
        uint *b = (uint *)bp2->data;
        for (int k = 0; k < NINDIRECT; k++) {
          if (b[k]) bfree(ip->dev, b[k]);
        }
        brelse(bp2);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }
  ```

---

### 2.4. Mở rộng đĩa (`kernel/param.h`) & Đồng bộ hóa `mkfs/mkfs.c`
* **Vấn đề kích thước phân vùng:** Giá trị `FSSIZE = 2000` blocks cũ chỉ tương đương ~2 MB, sau khi trừ các metadata blocks (boot, superblock, log 30 blocks, inode blocks, bitmap), không gian trống còn lại không đủ an toàn để chứa file 1 MB cùng các file nhị phân của người dùng.
* **Giải pháp:**
  - Nâng `FSSIZE = 20000` trong `kernel/param.h` (~20 MB).
  - Cập nhật hàm `iappend()` trong `mkfs/mkfs.c`: Bổ sung nhánh xử lý `else` cho vùng Doubly-indirect, cho phép `mkfs` tạo cây con trỏ 2 tầng khi ghi `user/largefile.bin` từ máy host vào file image `fs.img` lúc build.

---

### 2.5. Tái cấu trúc Web Server `user/httpd.c` — Đọc File Thật Từ Đĩa
* **Vấn đề cũ:** Trong Phase 1 sơ khai, khi nhận request `/file1mb`, server sử dụng hàm mock:
  ```c
  char gen_buf[BUF_SIZE];
  for (int b = 0; b < BUF_SIZE; b++) gen_buf[b] = (char)((b * 37 + 13) & 0xff);
  ```
  Dữ liệu được sinh trực tiếp trong RAM và đẩy ra socket. Điều này không phản ánh đúng hoạt động của một hệ điều hành thực tế (không tương tác với VFS, Buffer Cache và Disk Driver).
* **Giải pháp kỹ thuật:**
  - Xóa bỏ hoàn toàn nhánh kiểm tra `strcmp(path, "file1mb")` và vòng lặp mock byte ảo.
  - Server thực hiện đúng chuẩn HTTP/1.0:
    1. Nhận đường dẫn tệp tin từ HTTP Request Line (`GET /largefile.bin HTTP/1.1`).
    2. Mở tệp tin thật bằng `open(path, O_RDONLY)`. Nếu thất bại trả về `404 Not Found`.
    3. Gửi Header chuẩn: `HTTP/1.0 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n`.
    4. Sử dụng bộ đệm chunk `file_buf[BUF_SIZE]`, đọc dữ liệu đĩa theo từng block qua `read()` và truyền tuần tự qua socket bằng `write()`.
    5. Đóng file descriptor và đóng socket sạch sẽ sau khi hoàn tất.

---

### 2.6. Tự động hóa đóng gói `largefile.bin` trong `Makefile`
* Cập nhật `Makefile` để tự động sinh tệp dữ liệu ngẫu nhiên $1,048,576\text{ bytes}$ bằng `dd` và đóng gói vào `fs.img`:
  ```makefile
  fs.img: mkfs/mkfs README $(UPROGS) user/largefile.bin
  	mkfs/mkfs fs.img README $(UPROGS) user/largefile.bin

  user/largefile.bin:
  	dd if=/dev/urandom of=user/largefile.bin bs=1024 count=1024
  ```
* Bổ sung `user/largefile.bin` vào danh sách dọn dẹp của `clean:` target.

---

## 3. Kết Quả Đo Đạc Thực Nghiệm & Nghiệm Thu

### 3.1. Kiểm thử biên dịch sạch
```bash
make clean && make -j4
```
* **Kết quả:** Biên dịch thành công 100%, không xuất hiện bất kỳ cảnh báo (`-Wall -Werror`) hoặc lỗi biên dịch nào.

### 3.2. Kiểm thử tạo image hệ thống tệp với Doubly-Indirect
* Lệnh chạy tạo `fs.img`:
  ```bash
  make fs.img
  ```
* **Kết quả thực tế từ `mkfs`:**
  ```text
  dd if=/dev/urandom of=user/largefile.bin bs=1024 count=1024
  1024+0 records in
  1024+0 records out
  1048576 bytes (1.0 MB, 1.0 MiB) copied, 0.00538732 s, 210 MB/s
  mkfs/mkfs fs.img README user/_cat user/_echo ... user/largefile.bin
  nmeta 49 (boot, super, log blocks 31, inode blocks 13, bitmap blocks 3) blocks 19951 total 20000
  balloc: first 2094 blocks have been allocated
  balloc: write bitmap block at sector 46
  ```
* **Đánh giá:**
  - Tổng số blocks: **20,000 blocks** (khớp chính xác với `FSSIZE`).
  - Đã cấp phát thành công **2,094 blocks** đầu tiên, chứa đầy đủ các chương trình user-space và tệp tin `largefile.bin` (chiếm hơn 1,024 blocks qua cơ chế Doubly-indirect).
  - Không gặp bất kỳ lỗi `assertion failed` nào về căn chỉnh struct Inode.

### 3.3. Kiểm thử truyền tải tệp 1 MB qua mạng & Đối chiếu mã SHA-256
Khởi động xv6 trong QEMU và kích hoạt Web Server:
```sh
$ httpd
Starting xv6 httpd server (HTTP/1.0) on port 80...
```

Từ terminal máy Host Linux, thực hiện tải tệp `largefile.bin` và đối chiếu mã SHA-256:
```bash
# 1. Tải tệp qua HTTP từ Web Server xv6
curl -v http://127.0.0.1:20080/largefile.bin -o dl.bin

# 2. Đối chiếu mã băm với tệp gốc trên đĩa host
sha256sum user/largefile.bin dl.bin
```

* **Kết quả đối chiếu:**
  ```text
  00f42d681e94388765453488fa6641659c00b285b8f93b23b06748255adfa1ce  user/largefile.bin
  00f42d681e94388765453488fa6641659c00b285b8f93b23b06748255adfa1ce  dl.bin
  ```

* **Đánh giá chất lượng:**
  1. **Độ toàn vẹn dữ liệu:** Mã băm SHA-256 trùng khớp 100% từng ký tự (`00f42d68...`). Toàn bộ $1,048,576\text{ bytes}$ dữ liệu đọc từ đĩa ảo xv6 qua VirtIO-Disk driver, chuyển qua Socket VFS, đóng gói vào các segment TCP, truyền qua card mạng Intel E1000 và ghi xuống Host đều nguyên vẹn, không suy hao hay sai lệch bit.
  2. **Hiệu năng & Ổn định:** Web Server đọc file theo khối và đẩy qua socket với cơ chế `mbufalloc_wait()`, không bị cạn kiệt bộ nhớ hay rơi vào Deadlock. Sau khi hoàn thành phiên truyền, kết nối được đóng sạch sẽ, server sẵn sàng nhận các yêu cầu tiếp theo.

---

## 4. Danh Sách Các File Mã Nguồn Đã Thay Đổi

| File | Thành phần | Nội dung thay đổi chính |
| :--- | :--- | :--- |
| `kernel/fs.h` | Cấu trúc đĩa | Định nghĩa `NDIRECT = 11`, thêm `NDINDIRECT`, `MAXFILE = 65803`. Cấu trúc `dinode` chứa `addrs[NDIRECT + 2]` giữ kích thước 64 bytes chuẩn. |
| `kernel/file.h` | Cấu trúc bộ nhớ | Cập nhật `struct inode` có mảng con trỏ `addrs[NDIRECT + 2]` đồng bộ với đĩa. |
| `kernel/param.h` | Tham số hệ thống | Nâng `FSSIZE = 20000` blocks (~20 MB) đảm bảo đủ dung lượng lưu trữ. |
| `kernel/fs.c` | Hệ thống tệp | Hiện thực ánh xạ cấp phát khối 2 tầng trong `bmap()` và giải phóng bộ nhớ đĩa đệ quy 2 cấp trong `itrunc()`. |
| `mkfs/mkfs.c` | Công cụ định dạng | Bổ sung xử lý vùng Doubly-indirect trong `iappend()`, cho phép nạp tệp lớn vào `fs.img`. |
| `user/httpd.c` | Web Server HTTP | Xóa bỏ mã giả lập byte ảo trong RAM; chuyển sang mở tệp thật và đọc dữ liệu qua `open()` / `read()`. |
| `Makefile` | Cấu hình Build | Thêm lệnh sinh tự động `user/largefile.bin` (1 MB) bằng `dd`, nhúng vào `fs.img`, dọn dẹp trong target `clean`. |
