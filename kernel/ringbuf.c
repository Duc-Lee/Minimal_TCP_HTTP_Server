// ============================================================================
// Thành viên 3 (TV3): Tầng Socket VFS, Web Server httpd & Test/Demo
// File: kernel/ringbuf.c
// Nhiệm vụ: Bộ đệm vòng ring_buf với cơ chế đồng bộ hóa sleep/wakeup
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

void
ringbuf_init(struct ringbuf *rb)
{
  initlock(&rb->lock, "ringbuf");
  acquire(&rb->lock);
  rb->head = 0;
  rb->tail = 0;
  rb->count = 0;
  rb->eof = 0;
  release(&rb->lock);
}

// Số byte dữ liệu hiện có trong ring buffer
int
ringbuf_avail(struct ringbuf *rb)
{
  acquire(&rb->lock);
  int count = rb->count;
  release(&rb->lock);
  return count;
}

// Số byte dung lượng còn trống trong ring buffer
int
ringbuf_free_space(struct ringbuf *rb)
{
  acquire(&rb->lock);
  int free_bytes = RINGBUF_SIZE - rb->count;
  release(&rb->lock);
  return free_bytes;
}

// Ghi dữ liệu vào ring buffer (nếu đầy sẽ sleep chờ có chỗ trống)
int
ringbuf_write(struct ringbuf *rb, const char *data, int len)
{
  int written = 0;

  acquire(&rb->lock);
  while (written < len) {
    while (rb->count == RINGBUF_SIZE) {
      // Buffer đã đầy: sleep trên con trỏ rb chờ người đọc giải phóng
      sleep_prepare(rb);
      release(&rb->lock);
      sleep();
      acquire(&rb->lock);
    }

    int space = RINGBUF_SIZE - rb->count;
    int to_write = len - written;
    if (to_write > space)
      to_write = space;

    for (int i = 0; i < to_write; i++) {
      rb->buf[rb->head] = data[written++];
      rb->head = (rb->head + 1) % RINGBUF_SIZE;
      rb->count++;
    }

    // Đánh thức bên đọc đang sleep
    wakeup(rb);
  }
  release(&rb->lock);

  return written;
}

// Đọc dữ liệu từ ring buffer (nếu rỗng sẽ sleep chờ có dữ liệu)
int
ringbuf_read(struct ringbuf *rb, char *data, int len)
{
  int read_bytes = 0;

  acquire(&rb->lock);
  while (rb->count == 0) {
    if (rb->eof) {
      release(&rb->lock);
      return 0; // Đã nhận FIN/đóng kết nối (EOF)
    }
    // Buffer rỗng: sleep chờ bên ghi nạp dữ liệu
    sleep_prepare(rb);
    release(&rb->lock);
    sleep();
    acquire(&rb->lock);
  }

  int to_read = len;
  if (to_read > rb->count)
    to_read = rb->count;

  for (int i = 0; i < to_read; i++) {
    data[read_bytes++] = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % RINGBUF_SIZE;
    rb->count--;
  }

  // Đánh thức bên ghi đang sleep
  wakeup(rb);
  release(&rb->lock);

  return read_bytes;
}
