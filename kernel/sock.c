// ============================================================================
// Thành viên 3 (TV3): Tầng Socket VFS, Web Server httpd & Test/Demo
// File: kernel/sock.c
// Nhiệm vụ: Tầng Socket VFS, gắn Socket vào struct file (read/write/close)
// ============================================================================

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

// Cấp phát cấu trúc socket
struct socket*
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

// Cấp phát cấu trúc socket cho một PCB đã có sẵn (khi accept)
struct socket*
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

// Báo hiệu EOF vào socket rx ringbuf khi đối phương gửi FIN
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

// Đóng socket và giải phóng tài nguyên
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
    release(&s->lock);

    if (pcb) {
      acquire(&pcb->lock);
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

    acquire(&sock_table.lock);
    s->next = sock_table.free_list;
    sock_table.free_list = s;
    release(&sock_table.lock);
    return;
  }
  release(&s->lock);
}

// Hàm đọc dữ liệu từ socket qua giao diện struct file
int
sock_read(struct socket *s, uint64 addr, int n)
{
  char kbuf[256];
  int total_read = 0;

  if (!s || n <= 0)
    return 0;

  // Lần đọc đầu tiên: sleep chờ nếu ringbuf chưa có dữ liệu (block)
  int chunk = n;
  if (chunk > (int)sizeof(kbuf))
    chunk = sizeof(kbuf);

  int r = ringbuf_read(&s->rx_buf, kbuf, chunk);
  if (r <= 0)
    return r;

  if (either_copyout(1, addr, kbuf, r) < 0)
    return -1;
  total_read += r;

  // Tiếp tục vét thêm dữ liệu NẾU trong ringbuf còn sẵn (non-blocking)
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

// Hàm ghi dữ liệu vào socket qua giao diện struct file
int
sock_write(struct socket *s, uint64 addr, int n)
{
  int total_written = 0;

  if (!s || !s->pcb || (s->pcb->state != TCP_ESTABLISHED && s->pcb->state != TCP_CLOSE_WAIT))
    return -1;

  struct tcp_pcb *pcb = s->pcb;

  while (total_written < n) {
    // Sliding Window flow control: giới hạn số lượng segment unacknowledged (in-flight)
    // để tránh tràn mbuf pool và thích ứng với Receive Window
    acquire(&pcb->lock);
    while (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT) {
      uint32 in_flight = pcb->snd_nxt - pcb->snd_una;
      // Cho phép tối đa 16 segments in-flight (~22 KB)
      if (in_flight < 16 * 1400) {
        break;
      }
      // Quá nhiều gói đang chờ ACK: sleep chờ ACK từ client giải phóng
      sleep_prepare(pcb);
      release(&pcb->lock);
      sleep();
      acquire(&pcb->lock);
    }
    release(&pcb->lock);

    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT)
      break;

    int chunk = n - total_written;
    if (chunk > 1400)
      chunk = 1400;

    struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEAD);
    if (!m) {
      // Tạm thời hết mbuf: chờ giải phóng mbuf từ ACK hoặc timer
      acquire(&pcb->lock);
      sleep_prepare(pcb);
      release(&pcb->lock);
      sleep();
      continue;
    }

    char *dst = mbufput(m, chunk);
    if (!dst) {
      mbuffree(m);
      break;
    }

    if (either_copyin(dst, 1, addr + total_written, chunk) < 0) {
      mbuffree(m);
      if (total_written == 0)
        return -1;
      break;
    }

    if (tcp_output(pcb, m, TCP_FLAG_ACK) < 0) {
      break;
    }

    total_written += chunk;
  }

  return total_written;
}

// Gắn socket vào một địa chỉ IP và cổng Port
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

// Chuyển socket sang trạng thái lắng nghe kết nối
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

// Chấp nhận kết nối từ hàng đợi backlog
struct socket*
sock_accept(struct socket *s)
{
  struct socket *newsock = 0;

  acquire(&s->lock);
  while (s->backlog_q == 0) {
    // Chưa có kết nối đến: sleep chờ bắt tay 3 bước hoàn tất
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

// Khởi tạo kết nối chủ động tới server từ xa
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
