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

  struct tcp_stat kstats[32];
  int count = tcp_get_stats(kstats, max_entries);
  if (count > 0) {
    if (either_copyout(1, uaddr, (void *)kstats, count * sizeof(struct tcp_stat)) < 0)
      return -1;
  }

  return count;
}
