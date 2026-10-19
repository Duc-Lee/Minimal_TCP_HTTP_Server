// ============================================================================
// Thành viên 2 (TV2): Lõi Giao vận TCP, RTO Timer
// File: kernel/tcp_timer.c
// Nhiệm vụ: Quản lý retransmit_queue & RTO động Jacobson/Karels
// Gọi từ trap.c mỗi timer interrupt (100Hz ở xv6)
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

#define RTO_MIN        200    // 200 ms
#define RTO_MAX        2000   // 2 giây (tránh timeout dài trên mạng LAN/QEMU)
#define TICKS_PER_SEC  100    // xv6 timer ticks mỗi giây

// Ticks sang ms
#define TICKS_TO_MS(t)  ((t) * (1000 / TICKS_PER_SEC))
// ms sang ticks (làm tròn lên)
#define MS_TO_TICKS(ms) (((ms) + (1000/TICKS_PER_SEC) - 1) / (1000/TICKS_PER_SEC))

// ─── Jacobson/Karels RTO Update ──────────────────────────────────────────────
// SRTT   = (7/8)*SRTT + (1/8)*RTT      (fixed-point, srtt lưu 8*SRTT thực)
// RTTVAR = (3/4)*RTTVAR + (1/4)*|SRTT-RTT| (fixed-point, rttvar lưu 4*RTTVAR)
// RTO    = SRTT/8 + RTTVAR
void
tcp_update_rto(struct tcp_pcb *pcb, uint32 rtt_msec)
{
  if (pcb->srtt == 0) {
    // Đo lần đầu tiên — RFC 6298 §2
    pcb->srtt   = rtt_msec << 3;             // srtt = 8 * RTT
    pcb->rttvar = (rtt_msec / 2) << 2;       // rttvar = 4 * (RTT/2)
  } else {
    int delta = (int)rtt_msec - (int)(pcb->srtt >> 3);
    pcb->srtt += delta;                       // (7/8)srtt + (1/8)rtt
    if (delta < 0) delta = -delta;
    delta -= (int)(pcb->rttvar >> 2);
    pcb->rttvar += delta;                     // (3/4)rttvar + (1/4)|delta|
  }

  uint32 rto = (pcb->srtt >> 3) + pcb->rttvar;
  if (rto < RTO_MIN) rto = RTO_MIN;
  if (rto > RTO_MAX) rto = RTO_MAX;
  pcb->rto = rto;
}

// ─── tcp_timer_tick: Gọi mỗi timer interrupt từ trap.c ──────────────────────
// Duyệt tất cả PCB đang active, kiểm tra retransmit deadline và TIME_WAIT
void
tcp_timer_tick(void)
{
  tcp_timer_process();
}

