// quan ly rto timer va retransmit timeout cho tcp
// goi dinh ky tu timer interrupt trong trap.c

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// RTO_MIN / RTO_MAX dinh nghia trong net.h
#define TICKS_PER_SEC  10     // timer xv6 chay o 10hz (1000000 cycles o 10MHz = 100ms)

// rfc 6298: gain alpha=1/8, beta=1/4, K=4 (dung phep dich bit)
#define RTT_ALPHA_SHIFT 3
#define RTT_BETA_SHIFT  2
#define RTT_K           4

#define TICKS_TO_MS(t)  ((t) * 100)
#define MS_TO_TICKS(ms) (((ms) + 99) / 100)

// tinh rto dong theo thuat toan jacobson/karels (rfc 6298)
// dung fixed-point: srtt nhan 8, rttvar nhan 4 de tranh dung so thuc
void
tcp_update_rto(struct tcp_pcb *pcb, uint32 rtt_msec)
{
  if (pcb->srtt == 0) {
    // khoi tao lan dau tien
    pcb->srtt = rtt_msec << RTT_ALPHA_SHIFT;
    pcb->rttvar = (rtt_msec / 2) << RTT_BETA_SHIFT;
  } else {
    int delta = (int)rtt_msec - (int)(pcb->srtt >> RTT_ALPHA_SHIFT);
    pcb->srtt += delta;
    if (delta < 0)
      delta = -delta;
    delta -= (int)(pcb->rttvar >> RTT_BETA_SHIFT);
    pcb->rttvar += delta;
  }

  // rto = srtt + K*rttvar; rttvar luu x4 nen K*rttvar/4 = rttvar
  uint32 rto = (pcb->srtt >> RTT_ALPHA_SHIFT) + pcb->rttvar;
  if (rto < RTO_MIN)
    rto = RTO_MIN;
  if (rto > RTO_MAX)
    rto = RTO_MAX;
  pcb->rto = rto;
}

// tinh lai rto tu srtt/rttvar hien co (khong dung mau rtt moi)
void
tcp_recalc_rto(struct tcp_pcb *pcb)
{
  uint32 rto = RTO_MIN;
  if (pcb->srtt != 0)
    rto = (pcb->srtt >> RTT_ALPHA_SHIFT) + pcb->rttvar;
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
