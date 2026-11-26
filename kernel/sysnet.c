// cac syscall mang: socket, bind, listen, accept, connect

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

static int
sock_fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

// tao socket va gan vao file descriptor
uint64
sys_socket(void)
{
  int domain, type, protocol;
  struct socket *s;
  struct file *f;
  int fd;

  argint(0, &domain);
  argint(1, &type);
  argint(2, &protocol);

  if ((f = filealloc()) == 0 || (fd = sock_fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    return -1;
  }

  s = sock_alloc();
  if (!s) {
    myproc()->ofile[fd] = 0;
    fileclose(f);
    return -1;
  }

  // gan socket struct vao file table voi type FD_SOCK
  f->type = FD_SOCK;
  f->readable = 1;
  f->writable = 1;
  f->sock = s;

  return fd;
}

// syscall: int bind(int sockfd, uint32 ip, uint16 port)
uint64
sys_bind(void)
{
  int fd;
  int ip;
  int port;
  struct file *f;
  struct proc *p = myproc();

  argint(0, &fd);
  argint(1, &ip);
  argint(2, &port);

  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_SOCK)
    return -1;

  return sock_bind(f->sock, (uint32)ip, (uint16)port);
}

// syscall: int listen(int sockfd, int backlog)
uint64
sys_listen(void)
{
  int fd, backlog;
  struct file *f;
  struct proc *p = myproc();

  argint(0, &fd);
  argint(1, &backlog);

  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_SOCK)
    return -1;

  return sock_listen(f->sock, backlog);
}

// syscall: int accept(int sockfd)
uint64
sys_accept(void)
{
  int fd;
  struct file *f, *newf;
  struct socket *newsock;
  int newfd;
  struct proc *p = myproc();

  argint(0, &fd);

  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_SOCK)
    return -1;

  newsock = sock_accept(f->sock);
  if (!newsock)
    return -1;

  if ((newf = filealloc()) == 0 || (newfd = sock_fdalloc(newf)) < 0) {
    if (newf)
      fileclose(newf);
    sock_close(newsock);
    return -1;
  }

  newf->type = FD_SOCK;
  newf->readable = 1;
  newf->writable = 1;
  newf->sock = newsock;

  return newfd;
}

// syscall: int connect(int sockfd, uint32 ip, uint16 port)
uint64
sys_connect(void)
{
  int fd;
  int ip;
  int port;
  struct file *f;
  struct proc *p = myproc();

  argint(0, &fd);
  argint(1, &ip);
  argint(2, &port);

  if (fd < 0 || fd >= NOFILE || (f = p->ofile[fd]) == 0 || f->type != FD_SOCK)
    return -1;

  return sock_connect(f->sock, (uint32)ip, (uint16)port);
}

// syscall: int set_loss_rate(int rate)
uint64
sys_set_loss_rate(void)
{
  int rate;
  argint(0, &rate);
  return e1000_set_loss_rate(rate);
}

// syscall: int get_loss_rate(void)
uint64
sys_get_loss_rate(void)
{
  return e1000_get_loss_rate();
}

// syscall: int netstat(void *buf, int max_entries)
uint64
sys_netstat(void)
{
  uint64 uaddr;
  int max_entries;

  argaddr(0, &uaddr);
  argint(1, &max_entries);

  // neu truyen buffer NULL (uaddr == 0) thi dump thong ke ra console kernel
  if (uaddr == 0) {
    tcp_dump_stats();
    return 0;
  }

  if (max_entries <= 0)
    return -1;

  if (max_entries > 32)
    max_entries = 32;

  // tuan 5: tcp_stat da them rtt_us/min_rtt_us/delivery_rate -> 32 entry ~2.3 KB.
  // khong dat tren kernel stack (chi 1 trang, con phai chua ngat e1000 long vao)
  // ma muon 1 trang rieng tu kalloc.
  struct tcp_stat *kstats = (struct tcp_stat *)kalloc();
  if (!kstats)
    return -1;
  int count = tcp_get_stats(kstats, max_entries);
  if (count > 0) {
    if (either_copyout(1, uaddr, (void *)kstats, count * sizeof(struct tcp_stat)) < 0) {
      kfree((void *)kstats);
      return -1;
    }
  }
  kfree((void *)kstats);

  return count;
}

// syscall: int set_cc(int algo) - chuyen doi thuat toan kiem soat tac nghen (0: Reno, 1: BBR)
uint64
sys_set_cc(void)
{
  int algo;
  argint(0, &algo);
  return tcp_set_cc_algo(algo);
}

// syscall: int get_cc(void) - lay thuat toan kiem soat tac nghen hien tai
uint64
sys_get_cc(void)
{
  return tcp_get_cc_algo();
}

// in bien ban kiem toan ra console kernel (memaudit(0, flags))
static void
net_audit_print(struct net_audit *a)
{
  printk("[memaudit] mbuf: total=%d free=%d walk=%d refcnt_used=%d bad=%d "
         "min_free=%d alloc_fail=%d double_free=%d\n",
         a->mbuf_total, a->mbuf_free, a->mbuf_free_walk, a->mbuf_refcnt_used,
         a->mbuf_bad, a->mbuf_min_free, a->mbuf_alloc_fail,
         a->mbuf_double_free);
  printk("[memaudit] e1000: rx_ring=%d tx_inflight=%d loss=%d%% "
         "tx_loss_sim=%d tx_ring_full=%d rx_nombuf=%d\n",
         a->rx_ring_held, a->tx_inflight, a->loss_rate, a->tx_loss_sim,
         a->tx_ring_full, a->rx_nombuf);
  printk("[memaudit] arp: valid=%d pending_entries=%d pending_mbufs=%d\n",
         a->arp_valid, a->arp_pending_entries, a->arp_pending_mbufs);
  printk("[memaudit] tcp: pcb_free=%d/%d listen=%d conns=%d time_wait=%d "
         "zombie=%d rtx_mbufs=%d ooo_mbufs=%d\n",
         a->tcp_pcb_free, a->tcp_pcb_total, a->tcp_listen, a->tcp_conns,
         a->tcp_time_wait, a->tcp_zombie, a->tcp_rtx_mbufs, a->tcp_ooo_mbufs);
  printk("[memaudit] accounted=%d/%d leaked=%d\n", a->accounted,
         a->mbuf_total, a->leaked);
}

// syscall: int memaudit(struct net_audit *buf, int flags)
// tuan 6 (TV1): kiem toan bo nho ngan xep mang. buf == 0 -> in ra console.
// tra ve so mbuf bi ro ri (leaked), 0 la sach.
uint64
sys_memaudit(void)
{
  uint64 uaddr;
  int flags;
  struct net_audit a;

  argaddr(0, &uaddr);
  argint(1, &flags);

  net_audit_collect(&a, flags);
  if (uaddr == 0) {
    net_audit_print(&a);
  } else if (either_copyout(1, uaddr, (void *)&a, sizeof(a)) < 0) {
    return -1;
  }
  return a.leaked;
}
