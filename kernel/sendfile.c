// syscall sendfile ho tro zero-copy tu buffer cache cua inode xuong socket

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

// syscall: int sendfile(int out_sock_fd, int in_file_fd, uint64 offset, int count)
uint64
sys_sendfile(void)
{
  int out_fd, in_fd, count;
  uint64 offset;
  struct file *f_out, *f_in;
  struct proc *p = myproc();

  argint(0, &out_fd);
  argint(1, &in_fd);
  argaddr(2, &offset);
  argint(3, &count);

  // kiem tra tinh hop le cua file descriptor dau ra (phai la socket)
  if (out_fd < 0 || out_fd >= NOFILE || (f_out = p->ofile[out_fd]) == 0 || f_out->type != FD_SOCK)
    return -1;

  // kiem tra tinh hop le cua file descriptor dau vao (phai la inode file)
  if (in_fd < 0 || in_fd >= NOFILE || (f_in = p->ofile[in_fd]) == 0 || f_in->type != FD_INODE)
    return -1;

  if (count <= 0)
    return 0;

  struct socket *s = f_out->sock;
  if (!s || !s->pcb)
    return -1;

  struct tcp_pcb *pcb = s->pcb;

  // neu socket da o trang thai dong thi bao loi
  if (pcb->state == TCP_CLOSED ||
      (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT))
    return -1;

  uint off = (uint)offset;
  if (off >= f_in->ip->size)
    return 0;

  if (off + count > f_in->ip->size)
    count = f_in->ip->size - off;

  int total_sent = 0;

  while (total_sent < count) {
    // flow control va congestion control: gioi han in-flight theo min(snd_wnd, cwnd)
    acquire(&pcb->lock);
    while (pcb->state == TCP_ESTABLISHED || pcb->state == TCP_CLOSE_WAIT) {
      uint32 in_flight = pcb->snd_nxt - pcb->snd_una;
      uint32 wnd = pcb->cwnd;
      if (wnd < 4 * TCP_MSS)
        wnd = 4 * TCP_MSS;
      if (in_flight < wnd)
        break;

      // doi ack hoac timer danh thuc de co them window
      sleep_prepare(pcb);
      release(&pcb->lock);
      sleep();
      acquire(&pcb->lock);
    }

    if (pcb->state != TCP_ESTABLISHED && pcb->state != TCP_CLOSE_WAIT) {
      release(&pcb->lock);
      if (total_sent == 0)
        return -1;
      break;
    }
    release(&pcb->lock);

    // chon kich thuoc chunk toi da 1400 bytes (TCP MSS)
    int chunk = count - total_sent;
    if (chunk > 1400)
      chunk = 1400;

    // cap phat mbuf cho segment moi
    struct mbuf *m = mbufalloc_wait(MBUF_DEFAULT_HEAD);
    if (!m) {
      if (total_sent == 0)
        return -1;
      break;
    }

    char *dst = mbufput(m, chunk);
    if (!dst) {
      mbuffree(m);
      if (total_sent == 0)
        return -1;
      break;
    }

    // doc truc tiep tu buffer cache dia vao payload mbuf (zero-copy khong qua user-space)
    ilock(f_in->ip);
    int bytes_read = readi(f_in->ip, 0, (uint64)dst, off + total_sent, chunk);
    iunlock(f_in->ip);

    if (bytes_read <= 0) {
      mbuffree(m);
      break;
    }

    // neu doc duoc it hon chunk du kien (o cuoi file) thi cat bot mbuf
    if (bytes_read < chunk) {
      mbuftrim(m, bytes_read);
    }

    // truyen segment xuong tang tcp
    if (tcp_output(pcb, m, TCP_FLAG_ACK) < 0) {
      if (total_sent == 0)
        return -1;
      break;
    }

    total_sent += bytes_read;
    if (bytes_read < chunk)
      break;
  }

  // cap nhat offset cua file dau vao neu da truyen duoc du lieu
  if (total_sent > 0)
    f_in->off = off + total_sent;

  return total_sent;
}
