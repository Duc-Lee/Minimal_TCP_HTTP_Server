#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    int cur = get_loss_rate();
    printf("Current E1000 packet loss rate: %d%%\n", cur);
    printf("Usage: setloss <rate_percent (0-100)>\n");
    exit(0);
  }

  int rate = atoi(argv[1]);
  if (rate < 0 || rate > 100) {
    printf("Error: loss rate must be between 0 and 100\n");
    exit(1);
  }

  set_loss_rate(rate);
  printf("E1000 packet loss rate set to %d%%\n", rate);
  exit(0);
}
