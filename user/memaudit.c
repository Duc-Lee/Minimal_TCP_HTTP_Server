// cong cu kiem toan bo nho ngan xep mang (tuan 6 - TV1)
//
// chung minh "zero memory leak": moi mbuf trong pool phai duoc tim thay o
// dung mot noi: free_list, rx/tx ring e1000, arp pending_queue, tcp
// retransmit_q hoac ooo_queue. o trang thai nghi (moi ket noi da dong):
//   mbuf_free_count + rx_ring == NMBUF, leaked == 0, khong co pcb zombie.
//
// cach dung:
//   memaudit            kiem toan 1 lan, in bien ban
//   memaudit -f         xoa sach bang arp truoc khi kiem toan
//   memaudit -w <giay>  cho toi da <giay> den khi ngan xep ve trang thai nghi
//                       roi moi chot bien ban (dung sau stress test)
//   memaudit -k         nho kernel in bien ban ra console (printk)
// tra ve exit(0) neu PASS, exit(1) neu FAIL.

#include "kernel/types.h"
#include "user/user.h"

#define POLL_TICKS 5 // 0.5 giay (xv6 chay 10 tick/giay)

// ngan xep da "nghi": khong con mbuf dang bay va khong con ket noi song.
// TIME_WAIT duoc chap nhan vi khong giu mbuf nao va se tu thu hoi sau 2 MSL.
static int
quiescent(struct net_audit *a)
{
  return a->leaked == 0 && a->tcp_zombie == 0 && a->tcp_rtx_mbufs == 0 &&
         a->tcp_ooo_mbufs == 0 && a->tx_inflight == 0 &&
         a->arp_pending_mbufs == 0 && a->tcp_conns == a->tcp_time_wait;
}

static void
report(struct net_audit *a)
{
  int in_use = a->mbuf_total - a->mbuf_free;

  printf("== memaudit: kiem toan bo nho ngan xep mang ==\n");
  printf("mbuf pool : total=%d free=%d (walk=%d) in_use=%d refcnt_used=%d "
         "bad=%d min_free=%d\n",
         a->mbuf_total, a->mbuf_free, a->mbuf_free_walk, in_use,
         a->mbuf_refcnt_used, a->mbuf_bad, a->mbuf_min_free);
  printf("            alloc_fail=%d wait_sleeps=%d double_free=%d\n",
         a->mbuf_alloc_fail, a->mbuf_wait_sleeps, a->mbuf_double_free);
  printf("e1000     : rx_ring=%d tx_inflight=%d loss=%d%% tx_pkts=%d "
         "tx_loss_sim=%d tx_ring_full=%d rx_pkts=%d rx_nombuf=%d\n",
         a->rx_ring_held, a->tx_inflight, a->loss_rate, a->tx_packets,
         a->tx_loss_sim, a->tx_ring_full, a->rx_packets, a->rx_nombuf);
  printf("arp       : valid=%d pending_entries=%d pending_mbufs=%d\n",
         a->arp_valid, a->arp_pending_entries, a->arp_pending_mbufs);
  printf("tcp       : pcb_free=%d/%d listen=%d conns=%d time_wait=%d "
         "zombie=%d rtx_mbufs=%d ooo_mbufs=%d\n",
         a->tcp_pcb_free, a->tcp_pcb_total, a->tcp_listen, a->tcp_conns,
         a->tcp_time_wait, a->tcp_zombie, a->tcp_rtx_mbufs, a->tcp_ooo_mbufs);
  printf("doi chieu : free(%d) + rx_ring(%d) + tx(%d) + arp(%d) + rtx(%d) + "
         "ooo(%d) = %d/%d\n",
         a->mbuf_free, a->rx_ring_held, a->tx_inflight, a->arp_pending_mbufs,
         a->tcp_rtx_mbufs, a->tcp_ooo_mbufs, a->accounted, a->mbuf_total);
}

// kiem tra cac dieu kien zero-leak, in tung loi. tra ve so loi.
static int
verdict(struct net_audit *a)
{
  int fail = 0;

  if (a->leaked != 0) {
    printf("  LOI: leaked=%d mbuf khong thuoc noi nao\n", a->leaked);
    fail++;
  }
  if (a->mbuf_free_walk != a->mbuf_free) {
    printf("  LOI: free_list dai %d != mbuf_free_count %d\n",
           a->mbuf_free_walk, a->mbuf_free);
    fail++;
  }
  if (a->mbuf_refcnt_used != a->mbuf_total - a->mbuf_free) {
    printf("  LOI: refcnt_used %d != total - free %d\n", a->mbuf_refcnt_used,
           a->mbuf_total - a->mbuf_free);
    fail++;
  }
  if (a->mbuf_bad != 0 || a->mbuf_double_free != 0) {
    printf("  LOI: mbuf hong ref_cnt=%d double_free=%d\n", a->mbuf_bad,
           a->mbuf_double_free);
    fail++;
  }
  if (a->tcp_zombie != 0) {
    printf("  LOI: %d pcb zombie (CLOSED, mo coi, khong duoc thu hoi)\n",
           a->tcp_zombie);
    fail++;
  }
  return fail;
}

int
main(int argc, char *argv[])
{
  int flags = 0;
  int wait_sec = 0;
  struct net_audit a;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-f") == 0) {
      flags |= NETAUDIT_FLUSH_ARP;
    } else if (strcmp(argv[i], "-k") == 0) {
      memaudit(0, flags);
      exit(0);
    } else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) {
      wait_sec = atoi(argv[++i]);
    } else {
      printf("usage: memaudit [-f] [-w seconds] [-k]\n");
      exit(1);
    }
  }

  memset(&a, 0, sizeof(a));
  if (memaudit(&a, 0) < 0 && a.mbuf_total == 0) {
    printf("memaudit: syscall that bai\n");
    exit(1);
  }

  // cho ngan xep xa het segment dang bay / ket noi dang dong
  int waited = 0;
  while (wait_sec > 0 && !quiescent(&a) && waited < wait_sec * 10) {
    pause(POLL_TICKS);
    waited += POLL_TICKS;
    memaudit(&a, 0);
  }
  if (wait_sec > 0)
    printf("memaudit: cho %d ms de ngan xep ve trang thai nghi (%s)\n",
           waited * 100, quiescent(&a) ? "nghi" : "van con hoat dong");

  // bien ban chot (co the kem xoa bang arp)
  if (flags)
    memaudit(&a, flags);

  report(&a);
  int fail = verdict(&a);

  // pool hoi phuc 100%: moi mbuf khong nam tren rx ring deu da ve free_list
  int restored = (a.mbuf_free + a.rx_ring_held == a.mbuf_total);
  if (quiescent(&a))
    printf("hoi phuc  : free + rx_ring = %d/%d -> %s\n",
           a.mbuf_free + a.rx_ring_held, a.mbuf_total,
           restored ? "100% (nguyen ven)" : "THIEU");
  if (quiescent(&a) && !restored)
    fail++;

  if (fail == 0)
    printf("memaudit: PASS leaked=0 zombie=0 (zero memory leak)\n");
  else
    printf("memaudit: FAIL leaked=%d zombie=%d loi=%d\n", a.leaked,
           a.tcp_zombie, fail);
  exit(fail == 0 ? 0 : 1);
}
