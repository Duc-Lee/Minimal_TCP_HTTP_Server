#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
usage(void)
{
  printf("Usage: setloss <rate_percent (0-100)> | status\n");
}

// kiem tra chuoi chi gom chu so (khong rong)
static int
is_number(char *s)
{
  if (*s == 0)
    return 0;
  for (; *s; s++) {
    if (*s < '0' || *s > '9')
      return 0;
  }
  return 1;
}

int
main(int argc, char *argv[])
{
  if (argc < 2) {
    int cur = get_loss_rate();
    printf("Current E1000 packet loss rate: %d%%\n", cur);
    usage();
    exit(0);
  }

  // setloss status: chi in ra, khong thay doi gi
  if (strcmp(argv[1], "status") == 0) {
    printf("Current E1000 packet loss rate: %d%%\n", get_loss_rate());
    exit(0);
  }

  if (!is_number(argv[1])) {
    printf("Error: invalid argument '%s'\n", argv[1]);
    usage();
    exit(1);
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
