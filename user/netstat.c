// cong cu netstat xem bang ket noi tcp va trang thai pcb

#include "kernel/types.h"
#include "user/user.h"

static const char *state_names[] = {
  "CLOSED",
  "LISTEN",
  "SYN_SENT",
  "SYN_RCVD",
  "ESTABLISHED",
  "FIN_WAIT_1",
  "FIN_WAIT_2",
  "CLOSE_WAIT",
  "CLOSING",
  "LAST_ACK",
  "TIME_WAIT"
};

// in chuoi va dem them khoang trang cho du do rong width
static void
print_pad(const char *s, int width)
{
  int len = strlen(s);
  printf("%s", s);
  for (int i = len; i < width; i++)
    printf(" ");
}

// chuyen so nguyen thanh chuoi thap phan
static void
itoa(int n, char *buf)
{
  char tmp[16];
  int i = 0, j = 0;
  if (n == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return;
  }
  int neg = 0;
  if (n < 0) {
    neg = 1;
    n = -n;
  }
  while (n > 0) {
    tmp[i++] = (n % 10) + '0';
    n /= 10;
  }
  if (neg)
    tmp[i++] = '-';
  while (i > 0)
    buf[j++] = tmp[--i];
  buf[j] = '\0';
}

// format dia chi ip:port thanh chuoi
static void
format_endpoint(char *buf, uint32 ip, uint16 port)
{
  char pbuf[16];
  itoa(port, pbuf);

  if (ip == 0) {
    char *prefix = "0.0.0.0:";
    int i = 0;
    while (prefix[i]) {
      buf[i] = prefix[i];
      i++;
    }
    if (port == 0) {
      buf[i++] = '*';
      buf[i] = '\0';
    } else {
      int j = 0;
      while (pbuf[j])
        buf[i++] = pbuf[j++];
      buf[i] = '\0';
    }
    return;
  }

  char b0[8], b1[8], b2[8], b3[8];
  itoa((ip >> 24) & 0xff, b0);
  itoa((ip >> 16) & 0xff, b1);
  itoa((ip >> 8) & 0xff, b2);
  itoa(ip & 0xff, b3);

  int idx = 0;
  for (int j = 0; b0[j]; j++) buf[idx++] = b0[j];
  buf[idx++] = '.';
  for (int j = 0; b1[j]; j++) buf[idx++] = b1[j];
  buf[idx++] = '.';
  for (int j = 0; b2[j]; j++) buf[idx++] = b2[j];
  buf[idx++] = '.';
  for (int j = 0; b3[j]; j++) buf[idx++] = b3[j];
  buf[idx++] = ':';
  for (int j = 0; pbuf[j]; j++) buf[idx++] = pbuf[j];
  buf[idx] = '\0';
}

int
main(int argc, char *argv[])
{
  // co -d de yeu cau kernel dump truc tiep ra console
  if (argc > 1 && strcmp(argv[1], "-d") == 0) {
    netstat(0, 0);
    exit(0);
  }

  struct tcp_stat stats[32];
  int count = netstat(stats, 32);

  if (count < 0) {
    printf("netstat: cannot read tcp stats\n");
    exit(1);
  }

  print_pad("Proto", 6);
  print_pad("Recv-Q", 8);
  print_pad("Send-Q", 8);
  print_pad("Local Address", 22);
  print_pad("Foreign Address", 22);
  print_pad("State", 13);
  print_pad("CWND", 8);
  print_pad("SSTHRESH", 10);
  print_pad("RTT", 6);
  print_pad("RTO", 6);
  printf("\n");

  char local_str[32];
  char remote_str[32];
  char num_buf[16];

  for (int i = 0; i < count; i++) {
    print_pad("tcp", 6);

    // Recv-Q
    itoa(0, num_buf);
    print_pad(num_buf, 8);

    // Send-Q (in-flight bytes = snd_nxt - snd_una)
    int send_q = (int)(stats[i].snd_nxt - stats[i].snd_una);
    if (send_q < 0 || stats[i].state == 1) // TCP_LISTEN
      send_q = 0;
    itoa(send_q, num_buf);
    print_pad(num_buf, 8);

    // Local Address
    format_endpoint(local_str, stats[i].local_ip, stats[i].local_port);
    print_pad(local_str, 22);

    // Foreign Address
    format_endpoint(remote_str, stats[i].remote_ip, stats[i].remote_port);
    print_pad(remote_str, 22);

    // State
    const char *st = (stats[i].state >= 0 && stats[i].state <= 10)
                     ? state_names[stats[i].state]
                     : "UNKNOWN";
    print_pad(st, 13);

    // CWND, SSTHRESH, RTT, RTO
    if (stats[i].state == 1) { // LISTEN
      print_pad("-", 8);
      print_pad("-", 10);
      print_pad("-", 6);
      print_pad("-", 6);
    } else {
      itoa(stats[i].cwnd, num_buf);
      print_pad(num_buf, 8);

      itoa(stats[i].ssthresh, num_buf);
      print_pad(num_buf, 10);

      itoa(stats[i].rtt, num_buf);
      print_pad(num_buf, 6);

      itoa(stats[i].rto, num_buf);
      print_pad(num_buf, 6);
    }
    printf("\n");
  }

  exit(0);
}
