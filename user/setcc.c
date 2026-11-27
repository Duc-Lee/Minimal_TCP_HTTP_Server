#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void
usage(void)
{
  printf("usage: setcc [0|1|2|reno|bbr|basic|status]\n");
}

static const char *
cc_name(int algo)
{
  if (algo == CC_BBR)
    return "BBR";
  if (algo == CC_BASIC)
    return "Basic";
  return "Reno";
}

int
main(int argc, char *argv[])
{
  if (argc < 2 || strcmp(argv[1], "status") == 0) {
    int cur = get_cc();
    printf("congestion control: %s (%d)\n", cc_name(cur), cur);
    if (argc < 2)
      usage();
    exit(0);
  }

  int algo = -1;
  if (strcmp(argv[1], "0") == 0 || strcmp(argv[1], "reno") == 0 || strcmp(argv[1], "RENO") == 0) {
    algo = CC_RENO;
  } else if (strcmp(argv[1], "1") == 0 || strcmp(argv[1], "bbr") == 0 || strcmp(argv[1], "BBR") == 0) {
    algo = CC_BBR;
  } else if (strcmp(argv[1], "2") == 0 || strcmp(argv[1], "basic") == 0 || strcmp(argv[1], "BASIC") == 0) {
    algo = CC_BASIC;
  } else {
    printf("invalid algorithm: %s\n", argv[1]);
    usage();
    exit(1);
  }

  if (set_cc(algo) < 0) {
    printf("setcc: failed to set congestion control algorithm\n");
    exit(1);
  }

  printf("set congestion control: %s (%d)\n", cc_name(algo), algo);
  exit(0);
}
