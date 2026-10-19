// ring buffer dung cho socket rx va tx co spinlock va sleep/wakeup

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

// ghi vao ring buffer, day thi sleep cho doc bot
int
ringbuf_write(struct ringbuf *rb, const char *data, int len)
{
  int written = 0;

  acquire(&rb->lock);
  while (written < len) {
    while (rb->count == RINGBUF_SIZE) {
      // day roi thi sleep cho doc
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

    // bao cho ben doc day
    wakeup(rb);
  }
  release(&rb->lock);

  return written;
}

// doc tu ring buffer, rong thi sleep cho den khi co data hoac eof
int
ringbuf_read(struct ringbuf *rb, char *data, int len)
{
  int read_bytes = 0;

  acquire(&rb->lock);
  while (rb->count == 0) {
    if (rb->eof) {
      release(&rb->lock);
      return 0; // da nhan fin hoac dong ket noi
    }
    // chua co data thi sleep cho
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

  // bao cho ben ghi biet da co cho trong
  wakeup(rb);
  release(&rb->lock);

  return read_bytes;
}
