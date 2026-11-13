// quan ly bo dem goi tin mbuf giong kieu sk_buff / bsd mbuf

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// kich thuoc pool mbuf. tuan 4: danh gia nhu cau khi 8 worker httpd cung gui
// du lieu voi TCP Reno/NewReno (cwnd mo rong toi 64 KB ~ 46 segment/ket noi):
//   rx ring e1000                     : 32 mbuf giu thuong truc
//   tx ring e1000                     : <= 31 mbuf (thuc te ~0-2 nho tx reclaim)
//   retransmit_q (8 ket noi dong thoi): ~16-46 segment/ket noi -> 128..368 mbuf
//   mbuf tam (rx dang xu ly, ack, arp, sock_write dang copy) : ~16
//   tong                              : ~207..447 -> chon 512 (~1.05 MB / 128 MB RAM)
#define MBUF_POOL_SIZE       512
#define NMBUF                MBUF_POOL_SIZE
#define MBUF_LOWAT           (NMBUF / 10) // canh bao khi mbuf ranh < 10% (51 mbuf)
#define MBUF_LOWAT_CLEAR     (NMBUF / 4)  // ranh >= 25% (128 mbuf) moi coi la hoi phuc (hysteresis)
#define MBUF_IDLE_BURST_MIN  64           // so lan cap phat toi thieu de bao cao giua dot tai
#define MBUF_IDLE_QUIET_TICKS 10          // 1 giay (10 tick): bao cao ngay khi ve idle sau khoang lang

// cac su kien can in ra console (in sau khi da nha lock)
enum {
  MBUF_EV_NONE = 0,
  MBUF_EV_LOWAT,      // vua tut duoi low watermark
  MBUF_EV_RECOVER,    // vua hoi phuc tren nguong clear
  MBUF_EV_IDLE,       // pool tro ve muc idle sau mot dot tai
  MBUF_EV_ALLOC_FAIL, // mbufalloc() that bai vi het pool
  MBUF_EV_DOUBLE_FREE // free mbuf da nam san trong pool (bug cua caller)
};

static struct {
  struct spinlock lock;
  struct mbuf mbufs[NMBUF];
  struct mbuf *free_list;

  // thong ke, deu duoc bao ve boi lock
  int  mbuf_free_count; // so mbuf dang ranh trong pool
  int  min_free;        // muc ranh thap nhat tu luc boot
  uint alloc_fail;      // so lan mbufalloc() tra ve 0
  uint wait_sleeps;     // so lan mbufalloc_wait() phai ngu cho
  uint double_free;     // so lan free trung
  int  lowat_active;    // 1 khi dang o duoi low watermark
  int  idle_free;       // muc ranh cao nhat sau free (= NMBUF - RX_RING_SIZE)
  int  burst_min;       // muc ranh thap nhat tu lan bao cao idle gan nhat
  uint burst_allocs;    // so lan cap phat tu lan bao cao idle gan nhat
  uint last_idle_tick;  // tick cua lan bao cao idle gan nhat
} mbuf_pool;

// anh chup thong ke de in ngoai lock
struct mbuf_snap {
  int  ev;
  int  free;
  int  min_free;
  int  burst_used;
  uint alloc_fail;
  uint wait_sleeps;
  uint double_free;
};

static int
is_pow2(uint n)
{
  return n != 0 && (n & (n - 1)) == 0;
}

static void
mbuf_snapshot_locked(struct mbuf_snap *s, int ev)
{
  s->ev = ev;
  s->free = mbuf_pool.mbuf_free_count;
  s->min_free = mbuf_pool.min_free;
  s->alloc_fail = mbuf_pool.alloc_fail;
  s->wait_sleeps = mbuf_pool.wait_sleeps;
  s->double_free = mbuf_pool.double_free;
}

// in su kien ra console. goi sau khi da release mbuf_pool.lock
static void
mbuf_report(struct mbuf_snap *s)
{
  switch (s->ev) {
  case MBUF_EV_LOWAT:
    printk("[mbuf] WARN low watermark: free=%d/%d (<%d), min_free=%d, "
           "alloc_fail=%d\n",
           s->free, NMBUF, MBUF_LOWAT, s->min_free, s->alloc_fail);
    break;
  case MBUF_EV_RECOVER:
    printk("[mbuf] recovered: free=%d/%d, min_free=%d, alloc_fail=%d, "
           "wait_sleeps=%d\n",
           s->free, NMBUF, s->min_free, s->alloc_fail, s->wait_sleeps);
    break;
  case MBUF_EV_IDLE:
    printk("[mbuf] idle: free=%d/%d, burst_used=%d, min_free=%d, "
           "alloc_fail=%d\n",
           s->free, NMBUF, s->burst_used, s->min_free, s->alloc_fail);
    break;
  case MBUF_EV_ALLOC_FAIL:
    printk("[mbuf] WARN pool empty: alloc_fail=%d, wait_sleeps=%d\n",
           s->alloc_fail, s->wait_sleeps);
    break;
  case MBUF_EV_DOUBLE_FREE:
    printk("[mbuf] BUG double free ignored (total=%d)\n", s->double_free);
    break;
  default:
    break;
  }
}

void
mbufinit(void)
{
  initlock(&mbuf_pool.lock, "mbuf_pool");
  acquire(&mbuf_pool.lock);
  mbuf_pool.free_list = 0;
  for (int i = 0; i < NMBUF; i++) {
    mbuf_pool.mbufs[i].next = mbuf_pool.free_list;
    mbuf_pool.mbufs[i].ref_cnt = 0;
    mbuf_pool.free_list = &mbuf_pool.mbufs[i];
  }
  mbuf_pool.mbuf_free_count = NMBUF;
  mbuf_pool.min_free = NMBUF;
  mbuf_pool.alloc_fail = 0;
  mbuf_pool.wait_sleeps = 0;
  mbuf_pool.double_free = 0;
  mbuf_pool.lowat_active = 0;
  mbuf_pool.idle_free = 0;
  mbuf_pool.burst_min = NMBUF;
  mbuf_pool.burst_allocs = 0;
  mbuf_pool.last_idle_tick = 0;
  release(&mbuf_pool.lock);
}

// lay 1 mbuf ra khoi free list va reset metadata. goi khi dang giu lock,
// free list phai khac rong. tra ve su kien low watermark neu co.
static int
mbuf_take_locked(struct mbuf **out, unsigned int headroom)
{
  struct mbuf *m = mbuf_pool.free_list;
  int ev = MBUF_EV_NONE;

  mbuf_pool.free_list = m->next;
  m->next = 0;
  m->ref_cnt = 1;
  m->seq = 0;
  m->seq_end = 0;
  m->retransmitted = 0;
  m->send_tick = 0;
  m->head = m->buf + headroom;
  m->len = 0;

  mbuf_pool.mbuf_free_count--;
  mbuf_pool.burst_allocs++;
  if (mbuf_pool.mbuf_free_count < mbuf_pool.min_free)
    mbuf_pool.min_free = mbuf_pool.mbuf_free_count;
  if (mbuf_pool.mbuf_free_count < mbuf_pool.burst_min)
    mbuf_pool.burst_min = mbuf_pool.mbuf_free_count;

  if (!mbuf_pool.lowat_active && mbuf_pool.mbuf_free_count < MBUF_LOWAT) {
    mbuf_pool.lowat_active = 1;
    ev = MBUF_EV_LOWAT;
  }

  *out = m;
  return ev;
}

// lay mbuf tu pool, chot san headroom de prepended header
struct mbuf *
mbufalloc(unsigned int headroom)
{
  struct mbuf *m = 0;
  struct mbuf_snap snap;

  if (headroom > MBUF_SIZE)
    return 0;

  snap.ev = MBUF_EV_NONE;
  acquire(&mbuf_pool.lock);
  if (mbuf_pool.free_list) {
    int ev = mbuf_take_locked(&m, headroom);
    if (ev != MBUF_EV_NONE)
      mbuf_snapshot_locked(&snap, ev);
  } else {
    mbuf_pool.alloc_fail++;
    // chi in o lan 1, 2, 4, 8... de khong lam ngap console
    if (is_pow2(mbuf_pool.alloc_fail))
      mbuf_snapshot_locked(&snap, MBUF_EV_ALLOC_FAIL);
  }
  release(&mbuf_pool.lock);

  mbuf_report(&snap);
  return m;
}

// cap phat mbuf cho process context, neu tam thoi het thi sleep cho giai phong
struct mbuf*
mbufalloc_wait(unsigned int headroom)
{
  struct mbuf *m = 0;
  struct mbuf_snap snap;

  if (headroom > MBUF_SIZE)
    return 0;

  snap.ev = MBUF_EV_NONE;
  acquire(&mbuf_pool.lock);
  while (mbuf_pool.free_list == 0) {
    mbuf_pool.wait_sleeps++;
    sleep_prepare(&mbuf_pool);
    release(&mbuf_pool.lock);
    sleep();
    acquire(&mbuf_pool.lock);
  }
  // dung chung mbuf_take_locked de reset day du metadata
  // (truoc day thieu retransmitted/send_tick -> co the lam sai quy tac karn)
  int ev = mbuf_take_locked(&m, headroom);
  if (ev != MBUF_EV_NONE)
    mbuf_snapshot_locked(&snap, ev);
  release(&mbuf_pool.lock);

  mbuf_report(&snap);
  return m;
}

// giai phong mbuf hoac giam ref count
void
mbuffree(struct mbuf *m)
{
  struct mbuf_snap snap;

  if (!m)
    return;

  snap.ev = MBUF_EV_NONE;
  acquire(&mbuf_pool.lock);
  if (m->ref_cnt <= 0) {
    // mbuf da nam trong pool: day vao lan nua se lam vong free list
    // va sai mbuf_free_count -> bo qua va bao loi
    mbuf_pool.double_free++;
    if (is_pow2(mbuf_pool.double_free))
      mbuf_snapshot_locked(&snap, MBUF_EV_DOUBLE_FREE);
    release(&mbuf_pool.lock);
    mbuf_report(&snap);
    return;
  }

  m->ref_cnt--;
  if (m->ref_cnt == 0) {
    m->next = mbuf_pool.free_list;
    mbuf_pool.free_list = m;
    mbuf_pool.mbuf_free_count++;

    if (mbuf_pool.lowat_active &&
        mbuf_pool.mbuf_free_count >= MBUF_LOWAT_CLEAR) {
      mbuf_pool.lowat_active = 0;
      mbuf_snapshot_locked(&snap, MBUF_EV_RECOVER);
    }

    // hoc muc ranh nen (NMBUF - RX_RING_SIZE) va bao cao khi pool hoi phuc
    // 100% ve muc nen sau mot dot tai hoac sau khoang lang.
    if (mbuf_pool.mbuf_free_count > mbuf_pool.idle_free) {
      mbuf_pool.idle_free = mbuf_pool.mbuf_free_count;
      mbuf_pool.burst_min = mbuf_pool.mbuf_free_count;
      mbuf_pool.burst_allocs = 0;
    } else if (mbuf_pool.mbuf_free_count == mbuf_pool.idle_free) {
      uint dt = ticks - mbuf_pool.last_idle_tick;
      if (snap.ev == MBUF_EV_NONE &&
          ((mbuf_pool.burst_allocs >= MBUF_IDLE_BURST_MIN && dt >= 5) ||
           (mbuf_pool.burst_allocs > 0 && dt >= MBUF_IDLE_QUIET_TICKS))) {
        int used = mbuf_pool.idle_free - mbuf_pool.burst_min;
        mbuf_snapshot_locked(&snap, MBUF_EV_IDLE);
        snap.burst_used = used;
        mbuf_pool.burst_min = mbuf_pool.mbuf_free_count;
        mbuf_pool.burst_allocs = 0;
        mbuf_pool.last_idle_tick = ticks;
      }
    }

    wakeup(&mbuf_pool); // danh thuc tien trinh dang cho mbuf
  }
  release(&mbuf_pool.lock);

  mbuf_report(&snap);
}

// day nguoc con tro head ve truoc de nhet header moi
char *
mbufpush(struct mbuf *m, unsigned int len)
{
  if (m->head - len < m->buf)
    return 0; // khong du headroom

  m->head -= len;
  m->len += len;
  return m->head;
}

// cat bot header o dau va dich head ra sau
char *
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
char *
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
