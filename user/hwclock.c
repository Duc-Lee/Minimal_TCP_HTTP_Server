// hwclock: kiem chung bo dem phan cung risc-v va dong ho do phan giai us
// (tuan 5 - TV1, ha tang thoi gian cho Google BBR va benchmark sendfile)
//
// cach dung:
//   hwclock            thong tin timebase, hieu chuan cycle/us, do phan giai,
//                      kiem tra don dieu, benchmark memmove 1 MB bang rdcycle
//   hwclock rtt [n]    in rtt_us / min_rtt_us / delivery rate cua cac ket noi
//                      tcp, lap n lan moi 500 ms (mac dinh 1 lan)

#include "kernel/types.h"
#include "user/user.h"

#define BENCH_CHUNK 4096
#define BENCH_TOTAL (1024 * 1024)

static char bench_src[BENCH_CHUNK];
static char bench_dst[BENCH_CHUNK];

// in so fixed-point x1000 dang a.bcd
static void
print_fp3(uint64 v)
{
  uint64 frac = v % 1000;
  printf("%lu.%lu%lu%lu", v / 1000, frac / 100, (frac / 10) % 10, frac % 10);
}

static void
show_info(void)
{
  printf("== hwclock: bo dem phan cung RISC-V ==\n");
  printf("timebase      : %lu Hz (%lu tick = 1 us, 1 tick = 100 ns)\n",
         (uint64)TIMEBASE_HZ, (uint64)TIMEBASE_PER_US);
  printf("time hien tai : %lu tick = %lu us\n", rdtime(), rdtime_us());
  printf("cycle hien tai: %lu\n", rdcycle());
  printf("instret       : %lu\n", rdinstret());
}

// hieu chuan cycle theo time trong 1 giay (pause 10 tick xv6)
static uint64
calibrate(void)
{
  int k0 = uptime();
  uint64 t0 = rdtime();
  uint64 c0 = rdcycle();
  pause(10);
  uint64 c1 = rdcycle();
  uint64 t1 = rdtime();
  int k1 = uptime();

  uint64 us = (t1 - t0) / TIMEBASE_PER_US;
  uint64 cyc = c1 - c0;
  uint64 fp = us ? (cyc * 1000) / us : 0;

  printf("\n== hieu chuan (pause 10 tick) ==\n");
  printf("xv6 ticks     : %d tick (~%d ms theo 100 ms/tick)\n", k1 - k0,
         (k1 - k0) * 100);
  printf("dong ho time  : %lu us\n", us);
  printf("cycle         : %lu chu ky\n", cyc);
  printf("ty le         : ");
  print_fp3(fp);
  printf(" cycle/us\n");
  return fp;
}

// do phan giai: hieu nho nhat khac 0 giua 2 lan doc lien tiep
static void
resolution(void)
{
  uint64 min_t = ~0UL, min_c = ~0UL;
  for (int i = 0; i < 2000; i++) {
    uint64 a = rdtime(), b = rdtime();
    if (b > a && b - a < min_t)
      min_t = b - a;
    uint64 c = rdcycle(), d = rdcycle();
    if (d > c && d - c < min_c)
      min_c = d - c;
  }
  printf("\n== do phan giai ==\n");
  printf("time  : buoc nho nhat %lu tick = %lu ns\n", min_t, min_t * 100);
  printf("cycle : buoc nho nhat %lu chu ky\n", min_c);
}

// kiem tra time/cycle khong bao gio lui (ke ca khi process doi hart)
static void
monotonic(void)
{
  int bad_t = 0, bad_c = 0;
  uint64 pt = rdtime(), pc = rdcycle();
  for (int i = 0; i < 20; i++) {
    pause(1); // nhuong cpu -> co the duoc lap lich sang hart khac
    for (int j = 0; j < 5000; j++) {
      uint64 t = rdtime(), c = rdcycle();
      if (t < pt)
        bad_t++;
      if (c < pc)
        bad_c++;
      pt = t;
      pc = c;
    }
  }
  printf("\n== kiem tra don dieu (100000 mau, 20 lan doi lich) ==\n");
  printf("time  lui: %d lan -> %s\n", bad_t, bad_t ? "FAIL" : "OK");
  printf("cycle lui: %d lan -> %s\n", bad_c, bad_c ? "FAIL" : "OK");
}

// do chi phi cpu cua memmove 1 MB (giong cach TV3 do sys_sendfile)
static void
bench(uint64 fp)
{
  for (int i = 0; i < BENCH_CHUNK; i++)
    bench_src[i] = (char)i;

  uint64 t0 = rdtime(), c0 = rdcycle();
  for (int off = 0; off < BENCH_TOTAL; off += BENCH_CHUNK)
    memmove(bench_dst, bench_src, BENCH_CHUNK);
  uint64 c1 = rdcycle(), t1 = rdtime();

  uint64 us = (t1 - t0) / TIMEBASE_PER_US;
  uint64 cyc = c1 - c0;
  printf("\n== benchmark memmove 1 MB (khoi 4 KB) ==\n");
  printf("cycle : %lu chu ky (%lu chu ky/KB)\n", cyc, cyc / 1024);
  printf("time  : %lu us", us);
  if (fp)
    printf(" (quy doi tu cycle: %lu us)", (cyc * 1000) / fp);
  printf("\n");
  if (us)
    printf("bang thong copy: %lu MB/s\n", (uint64)BENCH_TOTAL / us);
}

static const char *st_names[] = {
  "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD", "ESTAB", "FIN_WAIT_1",
  "FIN_WAIT_2", "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT"
};

// in thong so do thoi gian us cua cac ket noi tcp
static void
show_rtt(int rounds)
{
  static struct tcp_stat st[32];
  for (int r = 0; r < rounds; r++) {
    int n = netstat(st, 32);
    if (n < 0) {
      printf("hwclock: khong doc duoc netstat\n");
      return;
    }
    printf("[t=%lu ms] %d ket noi\n", rdtime_us() / 1000, n);
    for (int i = 0; i < n; i++) {
      if (st[i].state == 1) // bo qua socket LISTEN
        continue;
      const char *s = (st[i].state >= 0 && st[i].state <= 10)
                          ? st_names[st[i].state] : "???";
      printf("  :%d -> :%d %s rtt_us=%d min_rtt_us=%d rate=%d KB/s cwnd=%d "
             "srtt=%d ms rto=%d ms\n",
             st[i].local_port, st[i].remote_port, s, st[i].rtt_us,
             st[i].min_rtt_us, st[i].delivery_rate / 1024, st[i].cwnd,
             st[i].rtt, st[i].rto);
    }
    if (r + 1 < rounds)
      pause(5); // 500 ms
  }
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "rtt") == 0) {
    int rounds = argc > 2 ? atoi(argv[2]) : 1;
    if (rounds <= 0)
      rounds = 1;
    show_rtt(rounds);
    exit(0);
  }

  show_info();
  uint64 fp = calibrate();
  resolution();
  monotonic();
  bench(fp);
  exit(0);
}
