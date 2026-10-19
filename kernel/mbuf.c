// ============================================================================
// Thành viên 1 (TV1): Hạ tầng Mạng, Driver L2/L3 & Zero-Copy
// File: kernel/mbuf.c
// Nhiệm vụ: Quản lý bộ đệm struct mbuf (có biến đếm ref_cnt)
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

static struct {
  struct spinlock lock;
  struct mbuf mbufs[128];
  struct mbuf *free_list;
} mbuf_pool;

void
mbufinit(void)
{
  initlock(&mbuf_pool.lock, "mbuf_pool");
  acquire(&mbuf_pool.lock);
  mbuf_pool.free_list = 0;
  for (int i = 0; i < 128; i++) {
    mbuf_pool.mbufs[i].next = mbuf_pool.free_list;
    mbuf_pool.mbufs[i].ref_cnt = 0;
    mbuf_pool.free_list = &mbuf_pool.mbufs[i];
  }
  release(&mbuf_pool.lock);
}

// Cấp phát mbuf mới với khoảng trống headroom
struct mbuf*
mbufalloc(unsigned int headroom)
{
  struct mbuf *m;

  if (headroom > MBUF_SIZE)
    return 0;

  acquire(&mbuf_pool.lock);
  m = mbuf_pool.free_list;
  if (m) {
    mbuf_pool.free_list = m->next;
    m->next = 0;
    m->ref_cnt = 1;
    m->head = m->buf + headroom;
    m->len = 0;
  }
  release(&mbuf_pool.lock);

  return m;
}

// Giải phóng mbuf hoặc giảm ref_cnt
void
mbuffree(struct mbuf *m)
{
  if (!m)
    return;

  acquire(&mbuf_pool.lock);
  m->ref_cnt--;
  if (m->ref_cnt <= 0) {
    m->ref_cnt = 0;
    m->next = mbuf_pool.free_list;
    mbuf_pool.free_list = m;
  }
  release(&mbuf_pool.lock);
}

// Mở rộng phần đầu (header) mbuf về phía trước
char*
mbufpush(struct mbuf *m, unsigned int len)
{
  if (m->head - len < m->buf)
    return 0; // Tràn bộ đệm headroom

  m->head -= len;
  m->len += len;
  return m->head;
}

// Cắt bớt phần đầu (header) mbuf về phía sau
char*
mbufpull(struct mbuf *m, unsigned int len)
{
  char *temp = m->head;
  if (len > m->len)
    return 0;

  m->head += len;
  m->len -= len;
  return temp;
}

// Mở rộng dữ liệu về phía đuôi mbuf
char*
mbufput(struct mbuf *m, unsigned int len)
{
  char *temp = m->head + m->len;
  if ((m->head - m->buf) + m->len + len > MBUF_SIZE)
    return 0;

  m->len += len;
  return temp;
}

// Cắt ngắn đuôi mbuf
void
mbuftrim(struct mbuf *m, unsigned int len)
{
  if (len < m->len)
    m->len = len;
}
