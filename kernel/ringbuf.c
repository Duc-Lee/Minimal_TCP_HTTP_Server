// ring buffer dung cho socket rx va tx co spinlock va sleep/wakeup

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// khoi tao ring buffer voi spinlock
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

// so byte hien co san trong buffer
int
ringbuf_avail(struct ringbuf *rb)
{
  acquire(&rb->lock);
  int count = rb->count;
  release(&rb->lock);
  return count;
}

// so byte con trong de ghi
int
ringbuf_free_space(struct ringbuf *rb)
{
  acquire(&rb->lock);
  int free_bytes = RINGBUF_SIZE - rb->count;
  release(&rb->lock);
  return free_bytes;
}

// ghi vao ring buffer voi memmove toi uu, an toan spinlock
int
ringbuf_write(struct ringbuf *rb, const char *data, int len)
{
  if (!rb || !data || len <= 0)
    return 0;

  int written = 0;

  acquire(&rb->lock);
  while (written < len) {
    int space = RINGBUF_SIZE - rb->count;
    if (space <= 0) {
      // buffer day, danh thuc nguoi doc va thoat de tranh deadlock interrupt
      wakeup(rb);
      break;
    }

    int to_write = len - written;
    if (to_write > space)
      to_write = space;

    // copy thanh tung block lien tuc bang memmove thay vi vong lap tung byte
    int first = RINGBUF_SIZE - rb->head;
    if (first > to_write)
      first = to_write;

    memmove(&rb->buf[rb->head], &data[written], first);
    rb->head = (rb->head + first) & (RINGBUF_SIZE - 1);

    int second = to_write - first;
    if (second > 0) {
      memmove(&rb->buf[rb->head], &data[written + first], second);
      rb->head = (rb->head + second) & (RINGBUF_SIZE - 1);
    }

    rb->count += to_write;
    written += to_write;

    // danh thuc tien trinh dang cho doc
    wakeup(rb);
  }
  release(&rb->lock);

  return written;
}

// doc du lieu tu ring buffer, ho tro sleep/wakeup va toi uu memmove
int
ringbuf_read(struct ringbuf *rb, char *data, int len)
{
  if (!rb || !data || len <= 0)
    return 0;

  acquire(&rb->lock);
  while (rb->count == 0) {
    if (rb->eof) {
      release(&rb->lock);
      return 0; // da nhan fin hoac dong ket noi
    }
    // chua co data thi sleep cho den khi co data hoac eof
    sleep_prepare(rb);
    release(&rb->lock);
    sleep();
    acquire(&rb->lock);
  }

  int to_read = len;
  if (to_read > rb->count)
    to_read = rb->count;

  // copy thanh tung block lien tuc bang memmove
  int first = RINGBUF_SIZE - rb->tail;
  if (first > to_read)
    first = to_read;

  memmove(data, &rb->buf[rb->tail], first);
  rb->tail = (rb->tail + first) & (RINGBUF_SIZE - 1);

  int second = to_read - first;
  if (second > 0) {
    memmove(data + first, &rb->buf[rb->tail], second);
    rb->tail = (rb->tail + second) & (RINGBUF_SIZE - 1);
  }

  rb->count -= to_read;

  // bao cho ben ghi biet da co cho trong
  wakeup(rb);
  release(&rb->lock);

  return to_read;
}
