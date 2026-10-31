// socket layer tren vfs cho xv6

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "proc.h"
#include "net.h"

#define MAX_SOCKETS 16

static struct {
  struct spinlock lock;
  struct socket sockets[MAX_SOCKETS];
  struct socket *free_list;
} sock_table;

void
sock_init(void)
{
  initlock(&sock_table.lock, "sock_table");
  acquire(&sock_table.lock);
  sock_table.free_list = 0;
  for (int i = 0; i < MAX_SOCKETS; i++) {
    initlock(&sock_table.sockets[i].lock, "socket");
    sock_table.sockets[i].next = sock_table.free_list;
    sock_table.sockets[i].ref_cnt = 0;
    sock_table.free_list = &sock_table.sockets[i];
  }
  release(&sock_table.lock);
}

// lay socket trong tu bang
struct socket *
sock_alloc(void)
{
  struct socket *s = 0;

  acquire(&sock_table.lock);
  if (sock_table.free_list) {
    s = sock_table.free_list;
    sock_table.free_list = s->next;
    s->next = 0;
    s->ref_cnt = 1;
    s->state = 0;
    s->backlog_q = 0;
    s->backlog_cnt = 0;
    s->backlog_max = 0;
    ringbuf_init(&s->rx_buf);
    ringbuf_init(&s->tx_buf);
    s->pcb = tcp_pcb_alloc();
    if (s->pcb)
      s->pcb->sock = s;
  }
  release(&sock_table.lock);

  return s;
}

// cap phat socket struct cho pcb moi tao khi accept
struct socket *
sock_alloc_for_pcb(struct tcp_pcb *pcb)
{
  struct socket *s = 0;

  acquire(&sock_table.lock);
  if (sock_table.free_list) {
    s = sock_table.free_list;
    sock_table.free_list = s->next;
    s->next = 0;
    s->ref_cnt = 1;
    s->state = 0;
    s->backlog_q = 0;
    s->backlog_cnt = 0;
    s->backlog_max = 0;
    ringbuf_init(&s->rx_buf);
    ringbuf_init(&s->tx_buf);
    s->pcb = pcb;
    if (pcb)
      pcb->sock = s;
  }
  release(&sock_table.lock);

  return s;
}

// set co eof vao rx_buf khi remote gui fin
void
sock_push_eof(struct socket *s)
{
  if (!s)
    return;
  acquire(&s->rx_buf.lock);
  s->rx_buf.eof = 1;
  wakeup(&s->rx_buf);
  release(&s->rx_buf.lock);
}

// dong socket va giai phong pcb/socket struct
void
sock_close(struct socket *s)
{
  if (!s)
    return;

  acquire(&s->lock);
  s->ref_cnt--;
  if (s->ref_cnt <= 0) {
    struct tcp_pcb *pcb = s->pcb;
    s->pcb = 0;

    // giai phong cac ket noi con ton dong trong hang doi backlog neu co
    struct socket *q = s->backlog_q;
    s->backlog_q = 0;
    s->backlog_cnt = 0;
    release(&s->lock);

    while (q) {
      struct socket *next_q = q->next;
      q->next = 0;
      sock_close(q);
      q = next_q;
    }

    if (pcb) {
      acquire(&pcb->lock);
      // huy con tro sock nguoc lai de tranh use-after-free khi pcb con song
      pcb->sock = 0;
      if (pcb->state == TCP_ESTABLISHED) {
        pcb->state = TCP_FIN_WAIT_1;
        release(&pcb->lock);
        tcp_output(pcb, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
      } else if (pcb->state == TCP_CLOSE_WAIT) {
        pcb->state = TCP_LAST_ACK;
        release(&pcb->lock);
        tcp_output(pcb, 0, TCP_FLAG_FIN | TCP_FLAG_ACK);
      } else {
        release(&pcb->lock);
        tcp_pcb_free(pcb);
      }
    }

    // danh thuc bat ky tien trinh nao dang cho tren rx/tx buffer
    acquire(&s->rx_buf.lock);
    s->rx_buf.eof = 1;
    wakeup(&s->rx_buf);
    release(&s->rx_buf.lock);

    acquire(&s->tx_buf.lock);
    wakeup(&s->tx_buf);
    release(&s->tx_buf.lock);

    wakeup(s);

    // lam sach trang thai ring buffer truoc khi tra ve free_list
    ringbuf_init(&s->rx_buf);
    ringbuf_init(&s->tx_buf);

    acquire(&sock_table.lock);
    s->next = sock_table.free_list;
    sock_table.free_list = s;
    release(&sock_table.lock);
    return;
  }
  release(&s->lock);
}

// read du lieu tu socket vao user space
int
sock_read(struct socket *s, uint64 addr, int n)
{
  char kbuf[256];
  int total_read = 0;

  if (!s || n <= 0)
    return 0;

  // neu buffer da can va remote da gui fin hoac connection reset thi bao eof (0)
  if (ringbuf_avail(&s->rx_buf) == 0) {
    if (s->rx_buf.eof || !s->pcb || s->pcb->state == TCP_CLOSED)
      return 0;
  }

  // doc block dau tien, neu chua co data thi sleep cho
  int chunk = n;
  if (chunk > (int)sizeof(kbuf))
    chunk = sizeof(kbuf);

  int r = ringbuf_read(&s->rx_buf, kbuf, chunk);
  if (r <= 0)
    return r; // tra ve 0 khi client dong ket noi (eof)

  if (either_copyout(1, addr, kbuf, r) < 0)
    return -1;
  total_read += r;

  // neu van con data trong ringbuf thi doc tiep luon khong block
  while (total_read < n && ringbuf_avail(&s->rx_buf) > 0) {
    chunk = n - total_read;
    if (chunk > (int)sizeof(kbuf))
      chunk = sizeof(kbuf);

    r = ringbuf_read(&s->rx_buf, kbuf, chunk);
    if (r <= 0)
      break;

    if (either_copyout(1, addr + total_read, kbuf, r) < 0)
      return -1;
    total_read += r;
  }

  return total_read;
}

// ghi du lieu tu user space xuong socket
int
sock_write(struct socket *s, uint64 addr, int n)
{
  int total_written = 0;

  if (!s || !s->pcb)
    return -1;

  struct tcp_pcb *pcb = s->pcb;

  // socket da o trang thai dong thi bao loi epipe (-1)
  if (pcb->state == TCP_CLOSED ||
      (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT))
    return -1;

  while (total_written < n) {
    // flow control: gioi han so goi in-flight tranh tran buffer va nghen mang
    acquire(&pcb->lock);
    while (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT) {
      uint32 in_flight = pcb->snd_nxt - pcb->snd_una;
      // toi da 16 goi in-flight
      if (in_flight < 16 * 1400) {
        break;
      }
      // doi ack de co window moi gui tiep
      sleep_prepare(pcb);
      release(&pcb->lock);
      sleep();
      acquire(&pcb->lock);
    }

    // neu ket noi bi ngat hoac chuyen sang tcp_closed trong luc cho
    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT) {
      release(&pcb->lock);
      if (total_written == 0)
        return -1; // epipe: socket hong ngay truoc khi gui duoc byte nao
      break;
    }
    release(&pcb->lock);

    int chunk = n - total_written;
    if (chunk > 1400)
      chunk = 1400;

    struct mbuf *m = mbufalloc_wait(MBUF_DEFAULT_HEAD);
    if (!m) {
      if (total_written == 0)
        return -1;
      break;
    }

    char *dst = mbufput(m, chunk);
    if (!dst) {
      mbuffree(m);
      if (total_written == 0)
        return -1;
      break;
    }

    if (either_copyin(dst, 1, addr + total_written, chunk) < 0) {
      mbuffree(m);
      if (total_written == 0)
        return -1;
      break;
    }

    if (tcp_output(pcb, m, TCP_FLAG_ACK) < 0) {
      if (total_written == 0)
        return -1;
      break;
    }

    total_written += chunk;
  }

  return total_written;
}

// bind dia chi ip va port vao socket
int
sock_bind(struct socket *s, uint32 ip, uint16 port)
{
  acquire(&s->lock);
  if (s->pcb) {
    s->pcb->local_ip = ip;
    s->pcb->local_port = port;
  }
  release(&s->lock);
  return 0;
}

// chuyen socket sang trang thai listen
int
sock_listen(struct socket *s, int backlog)
{
  acquire(&s->lock);
  if (s->pcb) {
    s->pcb->state = TCP_LISTEN;
    s->backlog_max = backlog;
    tcp_pcb_set_listen(s->pcb);
  }
  release(&s->lock);
  return 0;
}

// lay ket noi tu hang doi backlog
struct socket *
sock_accept(struct socket *s)
{
  struct socket *newsock = 0;

  acquire(&s->lock);
  while (s->backlog_q == 0) {
    // chua co client thi sleep cho bat tay xong
    sleep_prepare(s);
    release(&s->lock);
    sleep();
    acquire(&s->lock);
  }

  newsock = s->backlog_q;
  s->backlog_q = newsock->next;
  newsock->next = 0;
  s->backlog_cnt--;
  release(&s->lock);

  return newsock;
}

// chu dong connect toi remote host
int
sock_connect(struct socket *s, uint32 ip, uint16 port)
{
  acquire(&s->lock);
  if (s->pcb) {
    s->pcb->remote_ip = ip;
    s->pcb->remote_port = port;
    s->pcb->state = TCP_SYN_SENT;
    tcp_pcb_insert(s->pcb);
    release(&s->lock);
    tcp_output(s->pcb, 0, TCP_FLAG_SYN);
    return 0;
  }
  release(&s->lock);
  return -1;
}
