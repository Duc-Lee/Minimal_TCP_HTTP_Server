#ifndef _USER_H_
#define _USER_H_

#include "kernel/types.h"

#define SBRK_ERROR ((char *)-1)

struct stat;

#ifndef _STRUCT_TCP_STAT_
#define _STRUCT_TCP_STAT_
struct tcp_stat {
  uint32 local_ip;
  uint16 local_port;
  uint32 remote_ip;
  uint16 remote_port;
  int    state;
  uint32 cwnd;
  uint32 ssthresh;
  uint32 rtt;
  uint32 rto;
  uint32 snd_una;
  uint32 snd_nxt;
  uint32 snd_wnd;
  uint32 rcv_wnd;
  uint32 dupack_cnt;
  int    backoff;
  // tuan 5: thong so thoi gian do phan giai us
  uint32 rtt_us;         // mau rtt gan nhat (us)
  uint32 min_rtt_us;     // min rtt cua so truot 10s (us)
  uint32 delivery_rate;  // bytes/s
  // tuan 5: thong so BBR & Congestion Control (TV2)
  int    cc_algo;        // 0: Reno, 1: BBR
  int    bbr_state;      // 0: STARTUP, 1: DRAIN, 2: PROBE_BW, 3: PROBE_RTT
  uint32 max_bw;         // BBR max bandwidth (bytes/s)
  uint32 bdp;            // BBR BDP (bytes)
};
#endif

#define CC_RENO      0
#define CC_BBR       1

#define BBR_STARTUP   0
#define BBR_DRAIN     1
#define BBR_PROBE_BW  2
#define BBR_PROBE_RTT 3

// tuan 5: doc truc tiep bo dem phan cung risc-v tu user-mode
// (kernel da bat scounteren CY|TM|IR trong trapinithart)
// guard dung chung voi kernel/riscv.h (ulib.c, usertests.c include ca hai)
#ifndef _HW_COUNTERS_COMMON_
#define _HW_COUNTERS_COMMON_
#define TIMEBASE_HZ     10000000UL // csr time cua qemu virt: 10 MHz
#define TIMEBASE_PER_US (TIMEBASE_HZ / 1000000UL)

static inline uint64
rdcycle(void)
{
  uint64 x;
  asm volatile("csrr %0, cycle" : "=r"(x));
  return x;
}
#endif // _HW_COUNTERS_COMMON_

static inline uint64
rdtime(void)
{
  uint64 x;
  asm volatile("csrr %0, time" : "=r"(x));
  return x;
}

static inline uint64
rdinstret(void)
{
  uint64 x;
  asm volatile("csrr %0, instret" : "=r"(x));
  return x;
}

static inline uint64
rdtime_us(void)
{
  return rdtime() / TIMEBASE_PER_US;
}

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int pipe(int *);
int write(int, const void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(const char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
char *sys_sbrk(int, int);
int pause(int);
int uptime(void);
int sync(void);
int socket(int domain, int type, int protocol);
int bind(int sockfd, int ip, int port);
int listen(int sockfd, int backlog);
int accept(int sockfd);
int connect(int sockfd, int ip, int port);
int set_loss_rate(int rate);
int get_loss_rate(void);
int netstat(void *buf, int max_entries);
int set_cc(int algo);
int get_cc(void);
int sendfile(int out_fd, int in_fd, uint64 offset, int count);

#define AF_INET     2
#define SOCK_STREAM 1
#define INADDR_ANY  0

// ulib.c
int stat(const char *, struct stat *);
char *strcpy(char *, const char *);
void *memmove(void *, const void *, int);
char *strchr(const char *, char c);
int strcmp(const char *, const char *);
char *gets(char *, int max);
uint strlen(const char *);
void *memset(void *, int, uint);
int atoi(const char *);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char *sbrk(int);
char *sbrklazy(int);

// printf.c
void fprintf(int, const char *, ...) __attribute__((format(printf, 2, 3)));
void printf(const char *, ...) __attribute__((format(printf, 1, 2)));

// umalloc.c
void *malloc(uint);
void free(void *);

#endif // _USER_H_
