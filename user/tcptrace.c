// cong cu tcptrace: dashboard giam sat truc quan trang thai tcp bang ma ansi va ascii bar chart

#include "kernel/types.h"
#include "user/user.h"

#define MAX_ENTRIES 32
#define BAR_WIDTH   30

static const char *st_names[] = {
  "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD", "ESTABLISHED",
  "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT"
};

static const char *bbr_phases[] = {
  "STARTUP", "DRAIN", "PROBE_BW", "PROBE_RTT"
};

// ve thanh bieu do ascii
static void
draw_bar(const char *name, uint val, uint max_val, int width, const char *unit)
{
  printf("%s [", name);
  int filled = 0;
  if (max_val > 0)
    filled = (val * width) / max_val;
  if (filled > width)
    filled = width;

  for (int i = 0; i < filled; i++)
    printf("#");
  for (int i = filled; i < width; i++)
    printf(".");
  printf("] %u %s\n", val, unit);
}

// in dia chi ip dang a.b.c.d
static void
print_ip(uint32 ip)
{
  printf("%d.%d.%d.%d",
         (ip >> 24) & 0xff,
         (ip >> 16) & 0xff,
         (ip >> 8) & 0xff,
         ip & 0xff);
}

int
main(int argc, char *argv[])
{
  int max_frames = 20; // mac dinh chay 20 khung hinh (10 giay) roi dung
  if (argc > 1) {
    max_frames = atoi(argv[1]);
  }

  struct tcp_stat st[MAX_ENTRIES];
  int frame = 0;

  while (max_frames == 0 || frame < max_frames) {
    int n = netstat(st, MAX_ENTRIES);
    if (n < 0) {
      printf("tcptrace: khong the doc thong ke netstat\n");
      exit(1);
    }

    // xoa man hinh va dua con tro ve dau trang
    printf("\033[2J\033[H");
    printf("tcptrace (frame %d)\n\n", frame + 1);

    int active_conns = 0;

    for (int i = 0; i < n; i++) {
      if (st[i].state == 0) // CLOSED
        continue;

      const char *sname = (st[i].state >= 0 && st[i].state <= 10) ? st_names[st[i].state] : "UNKNOWN";

      printf("conn %d: ", i + 1);
      print_ip(st[i].local_ip);
      printf(":%d -> ", st[i].local_port);
      print_ip(st[i].remote_ip);
      printf(":%d [%s]\n", st[i].remote_port, sname);

      // neu la ket noi dang truyen du lieu (ESTABLISHED / CLOSE_WAIT / FIN_WAIT)
      if (st[i].state == 4 || st[i].state == 7 || st[i].state == 5 || st[i].state == 6) {
        active_conns++;

        // thuat toan dieu khien tac nghen (CC)
        if (st[i].cc_algo == CC_BBR) {
          const char *phase = (st[i].bbr_state >= 0 && st[i].bbr_state <= 3) ? bbr_phases[st[i].bbr_state] : "BBR";
          printf("  cc: BBR (%s) | bw: %u KB/s | bdp: %u B\n",
                 phase, st[i].max_bw / 1024, st[i].bdp);
        } else if (st[i].cc_algo == CC_BASIC) {
          printf("  cc: Basic (Fixed Window)\n");
        } else {
          printf("  cc: Reno\n");
        }

        // thong so thoi gian va bang thong
        printf("  rtt: %u us | min_rtt: %u us | rate: %u KB/s\n",
               st[i].rtt_us, st[i].min_rtt_us, st[i].delivery_rate / 1024);

        uint32 in_flight = (st[i].snd_nxt >= st[i].snd_una) ? (st[i].snd_nxt - st[i].snd_una) : 0;
        printf("  inflight: %u B | dupack: %u | backoff: %d\n",
               in_flight, st[i].dupack_cnt, st[i].backoff);

        // ve bieu do thanh ascii cho cwnd, ssthresh, rto
        draw_bar("  cwnd    ", st[i].cwnd, 65535, BAR_WIDTH, "B");
        draw_bar("  ssthresh", st[i].ssthresh, 65535, BAR_WIDTH, "B");
        draw_bar("  rto     ", st[i].rto, 3000, BAR_WIDTH, "ms");
        printf("\n");
      }
    }

    if (active_conns == 0) {
      printf("  khong co ket noi active nao\n\n");
    }

    frame++;
    pause(5); // ngu 500ms giua cac lan refresh
  }

  exit(0);
}
