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

// ---------------------------------------------------------------------------
// tuan 5 (TV1): dong ho do phan giai micro giay ho tro Google BBR
//
// BBR (TV2, kernel/tcp_cc.c) can 2 dai luong do tu mang that:
//   - min_rtt : rtt nho nhat trong 10 giay gan nhat (do tre lan truyen)
//   - delivery rate: toc do byte duoc ack (de uoc luong max_bw)
// truoc tuan 5 rtt chi do bang ticks (100 ms/tick) nen moi mau deu >= 100 ms,
// vo nghia voi mang qemu co rtt ~1 ms. cac ham duoi day dung csr "time"
// (10 MHz, do phan giai 0.1 us) qua hwclock_now_us().
//
// quy uoc khoa: moi ham nhan pcb deu phai duoc goi khi dang giu pcb->lock.
// ---------------------------------------------------------------------------

// thoi gian hien tai (us) dung chung cho tang tcp
uint64
tcp_now_us(void)
{
  return hwclock_now_us();
}

// reset trang thai do thoi gian khi cap phat pcb moi
void
tcp_rate_init(struct tcp_pcb *pcb)
{
  pcb->latest_rtt_us = 0;
  pcb->min_rtt_us = 0;
  pcb->min_rtt_stamp_us = 0;
  pcb->min_rtt_expired = 0;
  pcb->rtt_samples = 0;
  pcb->delivered = 0;
  pcb->delivered_us = 0;
  pcb->first_sent_us = 0;
  pcb->delivery_rate = 0;
  pcb->rate_interval_us = 0;
  pcb->rate_delivered = 0;
}

// danh dau thoi diem gui lan dau cho segment (ban clone nam trong
// retransmit_q). goi TRUOC khi noi seg vao retransmit_q.
static void
tcp_rate_on_send_at(struct tcp_pcb *pcb, struct mbuf *seg, uint64 now)
{
  // khong con du lieu dang bay: bat dau khoang do moi, tranh tinh ca thoi
  // gian ung dung ngoi yen vao mau delivery rate
  if (pcb->retransmit_q == 0) {
    pcb->first_sent_us = now;
    pcb->delivered_us = now;
  }

  seg->send_us = now;
  seg->tx_delivered = pcb->delivered;
  seg->tx_delivered_us = pcb->delivered_us;
  seg->tx_first_sent_us = pcb->first_sent_us;
}

void
tcp_rate_on_send(struct tcp_pcb *pcb, struct mbuf *seg)
{
  tcp_rate_on_send_at(pcb, seg, tcp_now_us());
}

// chup lai thong tin do cua segment truoc khi no bi giai phong khoi queue
void
tcp_rate_snapshot(struct mbuf *seg, struct tcp_rate_sample *rs)
{
  rs->send_us = seg->send_us;
  rs->prior_delivered = seg->tx_delivered;
  rs->prior_us = seg->tx_delivered_us;
  rs->first_sent_us = seg->tx_first_sent_us;
}

// min_rtt da qua 10 giay chua duoc lam moi?
int
tcp_min_rtt_expired(struct tcp_pcb *pcb, uint64 now_us)
{
  return pcb->min_rtt_us != 0 &&
         now_us - pcb->min_rtt_stamp_us > TCP_MIN_RTT_WIN_US;
}

// cap nhat bo loc min rtt cua so truot 10 giay.
// tra ve 1 neu min_rtt cu da het han (BBR dung de vao pha PROBE_RTT).
int
tcp_update_min_rtt(struct tcp_pcb *pcb, uint32 rtt_us, uint64 now_us)
{
  int expired = tcp_min_rtt_expired(pcb, now_us);

  if (rtt_us == 0)
    rtt_us = 1;
  if (pcb->min_rtt_us == 0 || rtt_us <= pcb->min_rtt_us || expired) {
    pcb->min_rtt_us = rtt_us;
    pcb->min_rtt_stamp_us = now_us;
  }
  return expired;
}

// min rtt hien tai (us), 0 neu chua co mau nao
uint32
tcp_get_min_rtt_us(struct tcp_pcb *pcb)
{
  return pcb->min_rtt_us;
}

// goi moi khi snd_una tien len `acked` byte.
// rs: mau cua segment moi nhat duoc ack ma chua tung gui lai (0 neu khong co,
// vd moi segment duoc ack deu la ban truyen lai -> quy tac karn).
// cap nhat latest_rtt_us, min_rtt_us, delivered va delivery_rate.
// tra ve 1 neu co mau rtt moi.
int
tcp_rate_on_ack(struct tcp_pcb *pcb, uint32 acked, uint64 now_us,
                struct tcp_rate_sample *rs)
{
  pcb->delivered += acked;
  pcb->delivered_us = now_us;

  if (!rs || rs->send_us == 0 || now_us < rs->send_us)
    return 0;

  // 1. mau rtt do phan giai us
  uint64 rtt = now_us - rs->send_us;
  if (rtt == 0)
    rtt = 1;
  if (rtt > 0xffffffffULL)
    rtt = 0xffffffffULL;
  pcb->latest_rtt_us = (uint32)rtt;
  pcb->rtt_samples++;
  pcb->min_rtt_expired = tcp_update_min_rtt(pcb, (uint32)rtt, now_us);

  // 2. mau delivery rate (draft-cheng-iccrg-delivery-rate-estimation):
  //    rate = (delivered - P.delivered) / max(send_elapsed, ack_elapsed)
  //    lay max de khong uoc luong qua cao khi ack bi nen (ack compression)
  pcb->first_sent_us = rs->send_us;
  uint64 send_elapsed = rs->send_us - rs->first_sent_us;
  uint64 ack_elapsed = now_us - rs->prior_us;
  uint64 interval = send_elapsed > ack_elapsed ? send_elapsed : ack_elapsed;
  uint64 bytes = pcb->delivered - rs->prior_delivered;

  // khoang do ngan hon min_rtt la mau khong dang tin -> bo, giu rate cu
  if (interval == 0 || interval < pcb->min_rtt_us)
    return 1;

  uint64 rate = (bytes * 1000000ULL) / interval;
  if (rate > 0xffffffffULL)
    rate = 0xffffffffULL;
  pcb->delivery_rate = (uint32)rate;
  pcb->rate_interval_us = (uint32)interval;
  pcb->rate_delivered = (uint32)bytes;
  return 1;
}

// kiem tra tu dong bo loc min_rtt 10 giay va bo uoc luong delivery_rate luc boot
void
tcp_timer_selftest(void)
{
  struct tcp_pcb pcb;
  struct mbuf seg;
  struct tcp_rate_sample rs;

  memset(&pcb, 0, sizeof(pcb));
  memset(&seg, 0, sizeof(seg));
  tcp_rate_init(&pcb);

  // 1. Kiem tra cua so truot 10s min_rtt (10,000,000 us)
  int exp1 = tcp_update_min_rtt(&pcb, 1200, 1000000ULL);
  int exp2 = tcp_update_min_rtt(&pcb, 800, 2000000ULL);
  int exp3 = tcp_update_min_rtt(&pcb, 1500, 5000000ULL);
  if (exp1 != 0 || exp2 != 0 || exp3 != 0 || pcb.min_rtt_us != 800)
    panic("tcp_timer_selftest: min_rtt within 10s window");

  // Sau 10.5s ke tu moc 2,000,000 us -> cua so 10s het han, chap nhan mau 1400 us
  int exp4 = tcp_update_min_rtt(&pcb, 1400, 12500001ULL);
  if (exp4 != 1 || pcb.min_rtt_us != 1400 ||
      pcb.min_rtt_stamp_us != 12500001ULL)
    panic("tcp_timer_selftest: min_rtt 10s window expiry");

  // 2. Kiem tra do RTT & delivery_rate qua tcp_rate_on_send_at / tcp_rate_on_ack
  tcp_rate_init(&pcb);
  tcp_rate_on_send_at(&pcb, &seg, 100000ULL);
  tcp_rate_snapshot(&seg, &rs);
  // Sau 1000 us (1 ms), ACK xac nhan 14000 bytes (10 * MSS)
  tcp_rate_on_ack(&pcb, 14000, 101000ULL, &rs);
  if (pcb.latest_rtt_us != 1000 || pcb.min_rtt_us != 1000 ||
      pcb.delivered != 14000 || pcb.delivery_rate != 14000000)
    panic("tcp_timer_selftest: delivery_rate calculation");

  // 3. Kiem tra Quy tac Karn (rs = 0 khi segment bi truyen lai)
  tcp_rate_on_ack(&pcb, 1400, 102000ULL, 0);
  if (pcb.delivered != 15400 || pcb.rtt_samples != 1)
    panic("tcp_timer_selftest: karn rule on ack");

  printk("tcp_timer: selftest 10s min_rtt window & delivery_rate OK\n");
}
