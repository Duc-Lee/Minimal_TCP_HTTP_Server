// kiem soat tac nghen tcp (RFC 5681 - TCP Reno & Google BBR - tuan 5 TV2)

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// Gioi han cwnd toi da de tranh tran mbuf pool trong xv6 (512 mbufs tong cong)
#define MAX_CWND (32 * TCP_MSS)

// Thuat toan mac dinh he thong: BBR (co the doi bang syscall sys_set_cc)
int tcp_cc_algo = CC_BBR;

// In trace cwnd cho script plot_cwnd.py tren host
static inline void
tcp_cc_trace(struct tcp_pcb *pcb, const char *ev)
{
  printk("cwnd: t=%d %s cwnd=%d ssthresh=%d\n",
         ticks, ev, pcb->cwnd, pcb->ssthresh);
}

// Chuyen doi thuat toan kiem soat tac nghen toan cuc va cac PCB dang hoat dong
int
tcp_set_cc_algo(int algo)
{
  if (algo != CC_RENO && algo != CC_BBR)
    return -1;
  int old = tcp_cc_algo;
  tcp_cc_algo = algo;
  tcp_pcb_update_cc(algo);
  return old;
}

// Lay thuat toan kiem soat tac nghen hien tai
int
tcp_get_cc_algo(void)
{
  return tcp_cc_algo;
}

// Khoi tao cac thong so BBR cho PCB
static void
bbr_init(struct tcp_pcb *pcb)
{
  pcb->bbr_state = BBR_STARTUP;
  pcb->bbr_max_bw = 10000000; // Uoc luong khoi diem 10 MB/s (~80 Mbps tren QEMU)
  pcb->bbr_bdp = 16 * TCP_MSS;
  pcb->bbr_target_cwnd = 16 * TCP_MSS;
  pcb->bbr_cycle_idx = 0;
  pcb->bbr_cycle_stamp = tcp_now_us();
  pcb->bbr_rtt_stamp = 0;
  pcb->bbr_round_delivered = 0;
  pcb->bbr_round_count = 0;
  pcb->bbr_bw_plateau_cnt = 0;
  pcb->bbr_prev_max_bw = 0;
}

// Khoi tao cac thong so kiem soat tac nghen cho PCB
void
tcp_cc_init(struct tcp_pcb *pcb)
{
  pcb->cc_algo = tcp_cc_algo;
  pcb->cwnd = 16 * TCP_MSS;      // Initial window (22.4 KB)
  pcb->ssthresh = 65535;         // Slow start threshold ban dau
  pcb->dupack_cnt = 0;
  pcb->recover = pcb->iss;
  bbr_init(pcb);
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

// Xu ly ACK cho thuat toan Reno / NewReno (RFC 5681, RFC 6582)
static void
reno_on_ack(struct tcp_pcb *pcb, uint32 ack)
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
      pcb->recover = ack;
      tcp_cc_trace(pcb, "RECOVERY_EXIT");
    }
  } else {
    pcb->dupack_cnt = 0;
    // Sau RTO timeout trong cua so co nhieu goi bi mat (loss 20-50%):
    // Neu ACK moi van chua vuot qua moc recover (snd_nxt luc timeout) va
    // van con segment cu trong retransmit_q, truyen lai ngay segment dau
    // hang doi thay vi doi them 200ms cho moi lo hong trong cung cua so.
    if ((int32)(ack - pcb->recover) < 0 && pcb->retransmit_q) {
      tcp_fast_retransmit(pcb);
    } else {
      pcb->recover = ack;
    }
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

// Xu ly ACK cho thuat toan Google BBR
static void
bbr_on_ack(struct tcp_pcb *pcb, uint32 ack)
{
  uint32 old_cwnd = pcb->cwnd;

  // 1. Reset duplicate ack count khi nhan ACK moi tien bo
  // Neu van con lo hong tiep theo trong cung cua so [ack, recover) (Partial ACK
  // khi mat nhieu goi o muc loss 20-50%), truyen lai ngay segment dau hang doi
  pcb->dupack_cnt = 0;
  if ((int32)(ack - pcb->recover) < 0 && pcb->retransmit_q) {
    tcp_fast_retransmit(pcb);
  } else {
    pcb->recover = ack;
  }

  // 2. Cap nhat bo loc max_bw (uoc luong toc do truyen cuc dai)
  if (pcb->delivery_rate > pcb->bbr_max_bw) {
    pcb->bbr_max_bw = pcb->delivery_rate;
  }

  // 3. Tinh toan BDP = max_bw * min_rtt
  // Lay min_rtt tu cua so 10 giay do TV1 cung cap (hoac fallback sang latest_rtt_us)
  uint32 min_rtt = pcb->min_rtt_us;
  if (min_rtt == 0)
    min_rtt = pcb->latest_rtt_us ? pcb->latest_rtt_us : 1000;

  uint64 bdp = ((uint64)pcb->bbr_max_bw * (uint64)min_rtt) / 1000000ULL;
  if (bdp < 4 * TCP_MSS)
    bdp = 4 * TCP_MSS;
  pcb->bbr_bdp = (uint32)bdp;

  // 4. Theo doi cac vong round-trip
  if (pcb->delivered >= pcb->bbr_round_delivered) {
    pcb->bbr_round_count++;
    pcb->bbr_round_delivered = pcb->delivered + pcb->bbr_bdp;

    // Trong pha STARTUP: theo doi bbr_bw_plateau_cnt de phat hien day ong truyen
    if (pcb->bbr_state == BBR_STARTUP) {
      if (pcb->bbr_max_bw < (pcb->bbr_prev_max_bw * 5) / 4) {
        pcb->bbr_bw_plateau_cnt++;
      } else {
        pcb->bbr_bw_plateau_cnt = 0;
        pcb->bbr_prev_max_bw = pcb->bbr_max_bw;
      }
    }
  }

  // 5. May trang thai 4 pha Google BBR
  uint64 now = tcp_now_us();
  // Bang pacing gains cho 8 pha chu ky trong PROBE_BW (chia cho 8: 1.25, 0.75, 1.0, ...)
  static const uint32 bbr_pacing_gains[8] = { 10, 6, 8, 8, 8, 8, 8, 8 };

  switch (pcb->bbr_state) {
  case BBR_STARTUP:
    // Tang toc nhanh: moi ACK tang 1 MSS de nhanh chong lap day ong dan
    pcb->cwnd += TCP_MSS;
    // Neu bang thong dung lai (plateau >= 3 rounds) hoac cwnd dat nguong toi da
    if (pcb->bbr_bw_plateau_cnt >= 3 || pcb->cwnd >= MAX_CWND) {
      pcb->bbr_state = BBR_DRAIN;
      tcp_cc_trace(pcb, "BBR_DRAIN");
    }
    break;

  case BBR_DRAIN:
    // Xa hang doi da tich luy trong STARTUP ve lai BDP
    if (pcb->cwnd > pcb->bbr_bdp + 2 * TCP_MSS) {
      pcb->cwnd = pcb->bbr_bdp + 2 * TCP_MSS;
    }
    // Khi du lieu in-flight giam xuong <= BDP thi sang PROBE_BW
    uint32 in_flight = pcb->snd_nxt - pcb->snd_una;
    if (in_flight <= pcb->bbr_bdp || pcb->delivered >= pcb->bbr_round_delivered) {
      pcb->bbr_state = BBR_PROBE_BW;
      pcb->bbr_cycle_idx = 0;
      pcb->bbr_cycle_stamp = now;
      tcp_cc_trace(pcb, "BBR_PROBE_BW");
    }
    break;

  case BBR_PROBE_BW:
    // Kiem tra bo loc min_rtt 10s co bi het han khong (-> BBR_PROBE_RTT)
    if (pcb->min_rtt_expired) {
      pcb->bbr_state = BBR_PROBE_RTT;
      pcb->bbr_rtt_stamp = now;
      pcb->cwnd = 4 * TCP_MSS;
      tcp_cc_trace(pcb, "BBR_PROBE_RTT");
      break;
    }

    // Chuyen pha chu ky sau moi RTT (toi thieu 20ms)
    uint32 rtt_interval = min_rtt > 20000 ? min_rtt : 20000;
    if (now - pcb->bbr_cycle_stamp >= rtt_interval) {
      pcb->bbr_cycle_idx = (pcb->bbr_cycle_idx + 1) % 8;
      pcb->bbr_cycle_stamp = now;
    }

    // Dieu chinh cwnd theo gain cua pha chu ky hien tai
    uint32 gain = bbr_pacing_gains[pcb->bbr_cycle_idx];
    uint32 target = (pcb->bbr_bdp * gain) / 8 + 4 * TCP_MSS;
    if (pcb->cwnd < target) {
      pcb->cwnd += TCP_MSS;
    } else if (pcb->cwnd > target && pcb->cwnd > 16 * TCP_MSS) {
      pcb->cwnd -= TCP_MSS / 2;
    }
    break;

  case BBR_PROBE_RTT:
    // Thu hep cua so ve 4 MSS trong 200 ms de xa bo dem mang va do chinh xac min_rtt
    pcb->cwnd = 4 * TCP_MSS;
    if (now - pcb->bbr_rtt_stamp >= 200000ULL) {
      pcb->min_rtt_expired = 0;
      pcb->min_rtt_stamp_us = now;
      pcb->bbr_state = BBR_PROBE_BW;
      pcb->bbr_cycle_idx = 0;
      pcb->bbr_cycle_stamp = now;
      pcb->cwnd = pcb->bbr_bdp + 4 * TCP_MSS;
      tcp_cc_trace(pcb, "BBR_PROBE_BW");
    }
    break;
  }

  // Gioi han cua so phu hop voi bang thong va buffer QEMU
  if (pcb->bbr_state != BBR_PROBE_RTT && pcb->cwnd < 16 * TCP_MSS)
    pcb->cwnd = 16 * TCP_MSS;
  if (pcb->cwnd > MAX_CWND)
    pcb->cwnd = MAX_CWND;

  if ((pcb->cwnd / TCP_MSS) != (old_cwnd / TCP_MSS)) {
    const char *ev_name = (pcb->bbr_state == BBR_STARTUP) ? "BBR_STARTUP" :
                          (pcb->bbr_state == BBR_DRAIN) ? "BBR_DRAIN" :
                          (pcb->bbr_state == BBR_PROBE_BW) ? "BBR_PROBE_BW" : "BBR_PROBE_RTT";
    tcp_cc_trace(pcb, ev_name);
  }
}

// Xu ly khi nhan duoc ACK xac nhan du lieu moi (snd_una tien len)
void
tcp_cc_on_ack(struct tcp_pcb *pcb, uint32 ack)
{
  if (pcb->cc_algo == CC_BBR)
    bbr_on_ack(pcb, ack);
  else
    reno_on_ack(pcb, ack);
}

// Xu ly khi nhan duoc Duplicate ACK (ack == snd_una)
void
tcp_cc_on_dup_ack(struct tcp_pcb *pcb, uint32 ack)
{
  pcb->dupack_cnt++;

  if (pcb->cc_algo == CC_BBR) {
    // Google BBR: Mat goi KHONG phai la dau hieu tac nghen mang!
    // Khi nhan 3 Dup ACKs, lap tuc truyen lai goi bi thieu bang Fast Retransmit,
    // nhung TUYET DOI KHONG cat giam cwnd hay ssthresh, giu nguyen thong luong BDP.
    if (pcb->dupack_cnt == 3) {
      pcb->recover = pcb->snd_nxt;
      tcp_fast_retransmit(pcb);
      tcp_cc_trace(pcb, "BBR_FAST_RETX");
      wakeup(pcb);
    } else if (pcb->dupack_cnt > 3) {
      if ((pcb->dupack_cnt % 3) == 0)
        tcp_fast_retransmit(pcb);
      wakeup(pcb);
    }
    return;
  }

  // Reno / NewReno
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
    pcb->cwnd += TCP_MSS;
    if (pcb->cwnd > MAX_CWND)
      pcb->cwnd = MAX_CWND;
    // Neu ban tin Fast Retransmit bi bo gia lap mat goi drop o muc loss cao (20-50%),
    // moi 3 DupACK tiep theo truyen lai segment dau hang doi de khong phai cho RTO 200ms
    if ((pcb->dupack_cnt % 3) == 0)
      tcp_fast_retransmit(pcb);
    wakeup(pcb);
  }
}

// Xu ly khi het han bo dem thoi gian truyen lai (RTO Timeout)
void
tcp_cc_on_timeout(struct tcp_pcb *pcb)
{
  if (pcb->cc_algo == CC_BBR) {
    // Google BBR khi timeout: truyen lai goi bi thieu o dau hang doi,
    // tam thoi dua cwnd ve 4 MSS cho den khi co ACK dau tien tro lai,
    // giu nguyen bo loc bbr_max_bw chu khong chia doi nhu Reno.
    tcp_fast_retransmit(pcb);
    pcb->cwnd = 4 * TCP_MSS;
    pcb->dupack_cnt = 0;
    pcb->recover = pcb->snd_nxt;
    pcb->bbr_state = BBR_STARTUP;
    tcp_cc_trace(pcb, "BBR_TIMEOUT");
    return;
  }

  // Reno
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
