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
};
#endif

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
