// kiem soat tac nghen tcp (RFC 5681 - TCP Reno)

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// Gioi han cwnd toi da de tranh tran mbuf pool trong xv6 (128 mbufs tong cong)
#define MAX_CWND (32 * TCP_MSS)

// in trace cwnd cho script plot_cwnd.py tren host
static inline void
tcp_cc_trace(struct tcp_pcb *pcb, const char *ev)
{
  printk("cwnd: t=%d %s cwnd=%d ssthresh=%d\n",
         ticks, ev, pcb->cwnd, pcb->ssthresh);
}

// Khoi tao cac thong so kiem soat tac nghen cho PCB
void
tcp_cc_init(struct tcp_pcb *pcb)
{
  pcb->cwnd = 16 * TCP_MSS;      // Initial window (22.4 KB)
  pcb->ssthresh = 65535;         // Slow start threshold ban dau
  pcb->dupack_cnt = 0;
  pcb->recover = 0;
  tcp_cc_trace(pcb, "INIT");
}

// Truyen lai nhanh segment dau hang doi retransmit_q
void
tcp_fast_retransmit(struct tcp_pcb *pcb)
{
  if (!pcb->retransmit_q)
    return;

  struct mbuf *m = pcb->retransmit_q;
  m->retransmitted = 1; // Quy tac Karn: khong lay mau RTT tu goi truyen lai

  struct mbuf *copy = mbufalloc(MBUF_DEFAULT_HEAD);
  if (copy) {
    char *dst = mbufput(copy, m->len);
    if (dst) {
      memmove(dst, m->head, m->len);
      ip_output(copy, pcb->remote_ip, IP_PROTO_TCP);
    } else {
      mbuffree(copy);
    }
  }

  uint32 t = pcb->rto / 100;
  pcb->rto_deadline = ticks + (t ? t : 1);
}

// Xu ly khi nhan duoc ACK xac nhan du lieu moi (snd_una tien len)
void
tcp_cc_on_ack(struct tcp_pcb *pcb, uint32 ack)
{
  uint32 old_cwnd = pcb->cwnd;

  if (pcb->dupack_cnt >= 3) {
    if ((int32)(ack - pcb->recover) < 0) {
      // RFC 6582 (NewReno): Partial ACK!
      // Van con goi tin khac bi mat trong cung mot cua so truyen
      // Truyen lai ngay lap tuc goi tiep theo bi thieu ma KHONG can cho RTO timeout (200ms)!
      tcp_fast_retransmit(pcb);
      // Giu nguyen Fast Recovery va dat lai deadline RTO
      pcb->cwnd = pcb->ssthresh + 3 * TCP_MSS;
      uint32 t = pcb->rto / 100;
      pcb->rto_deadline = ticks + (t ? t : 1);
      tcp_cc_trace(pcb, "PARTIAL_ACK");
      wakeup(pcb);
      return;
    } else {
      // Full ACK: Toan bo du lieu gui truoc khi vao Fast Recovery da duoc ACK
      // Thoat Fast Recovery, deflate cua so ve ssthresh
      pcb->cwnd = pcb->ssthresh;
      pcb->dupack_cnt = 0;
      tcp_cc_trace(pcb, "RECOVERY_EXIT");
    }
  } else {
    pcb->dupack_cnt = 0;
  }

  if (pcb->cwnd < pcb->ssthresh) {
    // Slow Start: tang 1 MSS cho moi ACK xac nhan du lieu moi
    pcb->cwnd += TCP_MSS;
  } else {
    // Congestion Avoidance: tang MSS * MSS / cwnd cho moi ACK (AIMD)
    uint32 inc = (TCP_MSS * TCP_MSS) / pcb->cwnd;
    if (inc == 0)
      inc = 1;
    pcb->cwnd += inc;
  }

  if (pcb->cwnd > MAX_CWND)
    pcb->cwnd = MAX_CWND;

  if ((pcb->cwnd / TCP_MSS) != (old_cwnd / TCP_MSS)) {
    tcp_cc_trace(pcb, (pcb->cwnd < pcb->ssthresh) ? "SLOW_START" : "CONGESTION_AVOIDANCE");
  }
}

// Xu ly khi nhan duoc Duplicate ACK (ack == snd_una)
void
tcp_cc_on_dup_ack(struct tcp_pcb *pcb, uint32 ack)
{
  pcb->dupack_cnt++;

  if (pcb->dupack_cnt == 3) {
    // Fast Retransmit (RFC 5681):
    // Tinh toan FlightSize va giam nua nguong ssthresh (Multiplicative Decrease)
    uint32 flight = pcb->snd_nxt - pcb->snd_una;
    uint32 half = flight / 2;
    if (half < 4 * TCP_MSS)
      half = 4 * TCP_MSS;
    pcb->ssthresh = half;
    pcb->cwnd = pcb->ssthresh + 3 * TCP_MSS;
    pcb->recover = pcb->snd_nxt;

    // Gui lai ngay lap tuc goi tin bi thieu dau tien
    tcp_fast_retransmit(pcb);
    tcp_cc_trace(pcb, "FAST_RETRANSMIT");
    wakeup(pcb);
  } else if (pcb->dupack_cnt > 3) {
    // Fast Recovery (Window Inflation):
    // Moi Duplicate ACK tiep theo chung to 1 goi nua da roi khoi mang va nam o receiver
    pcb->cwnd += TCP_MSS;
    if (pcb->cwnd > MAX_CWND)
      pcb->cwnd = MAX_CWND;
    wakeup(pcb);
  }
}

// Xu ly khi het han bo dem thoi gian truyen lai (RTO Timeout)
void
tcp_cc_on_timeout(struct tcp_pcb *pcb)
{
  uint32 flight = pcb->snd_nxt - pcb->snd_una;
  uint32 half = flight / 2;
  if (half < 4 * TCP_MSS)
    half = 4 * TCP_MSS;
  pcb->ssthresh = half;
  pcb->cwnd = 4 * TCP_MSS;
  pcb->dupack_cnt = 0;
  pcb->recover = pcb->snd_nxt;
  tcp_cc_trace(pcb, "TIMEOUT");
}
