// quan ly bo dem goi tin mbuf giong kieu sk_buff / bsd mbuf

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

// lay mbuf tu pool, chot san headroom de prepended header
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

// giai phong mbuf hoac giam ref count
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

// day nguoc con tro head ve truoc de nhet header moi
char*
mbufpush(struct mbuf *m, unsigned int len)
{
  if (m->head - len < m->buf)
    return 0; // khong du headroom

  m->head -= len;
  m->len += len;
  return m->head;
}

// cat bot header o dau va dich head ra sau
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

// noi them du lieu vao duoi
char*
mbufput(struct mbuf *m, unsigned int len)
{
  char *temp = m->head + m->len;
  if ((m->head - m->buf) + m->len + len > MBUF_SIZE)
    return 0;

  m->len += len;
  return temp;
}

// cat bot du lieu o duoi
void
mbuftrim(struct mbuf *m, unsigned int len)
{
  if (len < m->len)
    m->len = len;
}
