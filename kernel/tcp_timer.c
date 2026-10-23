// quan ly rto timer va retransmit timeout cho tcp
// goi dinh ky tu timer interrupt trong trap.c

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

#define RTO_MIN        200    // rto toi thieu 200ms
#define RTO_MAX        2000   // rto toi da 2s de tranh doi qua lau
#define TICKS_PER_SEC  10     // timer xv6 chay o 10hz (1000000 cycles o 10MHz = 100ms)

#define TICKS_TO_MS(t)  ((t) * 100)
#define MS_TO_TICKS(ms) (((ms) + 99) / 100)

// tinh rto dong theo thuat toan jacobson/karels (rfc 6298)
// dung fixed-point: srtt nhan 8, rttvar nhan 4 de tranh dung so thuc
void
tcp_update_rto(struct tcp_pcb *pcb, uint32 rtt_msec)
{
  if (pcb->srtt == 0) {
    // khoi tao lan dau tien
    pcb->srtt = rtt_msec << 3;
    pcb->rttvar = (rtt_msec / 2) << 2;
  } else {
    int delta = (int)rtt_msec - (int)(pcb->srtt >> 3);
    pcb->srtt += delta;
    if (delta < 0)
      delta = -delta;
    delta -= (int)(pcb->rttvar >> 2);
    pcb->rttvar += delta;
  }

  uint32 rto = (pcb->srtt >> 3) + pcb->rttvar;
  if (rto < RTO_MIN)
    rto = RTO_MIN;
  if (rto > RTO_MAX)
    rto = RTO_MAX;
  pcb->rto = rto;
}

// xu ly timer tick moi lan co ngat timer
void
tcp_timer_tick(void)
{
  tcp_timer_process();
}
