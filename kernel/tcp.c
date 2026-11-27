// quan ly tcp pcb va bang bam ket noi
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

#define TCP_PCB_POOL_SIZE 32
#define TCP_HASH_SIZE     16

static struct {
  struct spinlock lock;
  struct tcp_pcb pcbs[TCP_PCB_POOL_SIZE];
  struct tcp_pcb *free_list;
  struct tcp_pcb *hash_table[TCP_HASH_SIZE];
  struct tcp_pcb *listen_list; // danh sach pcb dang listen
} tcp_mgr;

static inline uint32
tcp_hash_fn(uint32 local_ip, uint16 local_port, uint32 remote_ip,
            uint16 remote_port)
{
  return (local_ip ^ remote_ip ^ local_port ^ remote_port) % TCP_HASH_SIZE;
}

void
tcp_init(void)
{
  initlock(&tcp_mgr.lock, "tcp_mgr");
  acquire(&tcp_mgr.lock);

  tcp_mgr.free_list = 0;
  tcp_mgr.listen_list = 0;
  for (int i = 0; i < TCP_PCB_POOL_SIZE; i++) {
    initlock(&tcp_mgr.pcbs[i].lock, "tcp_pcb");
    tcp_mgr.pcbs[i].next = tcp_mgr.free_list;
    tcp_mgr.pcbs[i].state = TCP_CLOSED;
    tcp_mgr.free_list = &tcp_mgr.pcbs[i];
  }
  for (int i = 0; i < TCP_HASH_SIZE; i++)
    tcp_mgr.hash_table[i] = 0;

  release(&tcp_mgr.lock);
  tcp_timer_selftest();
}

// go pcb khoi hash_table va listen_list (phai giu tcp_mgr.lock)
static void
tcp_pcb_detach_locked(struct tcp_pcb *pcb)
{
  uint32 h = tcp_hash_fn(pcb->local_ip, pcb->local_port, pcb->remote_ip,
                         pcb->remote_port);
  struct tcp_pcb **curr = &tcp_mgr.hash_table[h];
  while (*curr) {
    if (*curr == pcb) {
      *curr = pcb->next;
      break;
    }
    curr = &(*curr)->next;
  }

  curr = &tcp_mgr.listen_list;
  while (*curr) {
    if (*curr == pcb) {
      *curr = pcb->next;
      break;
    }
    curr = &(*curr)->next;
  }
}

// tai su dung PCB TIME_WAIT lau nhat khi free_list can kiet duoi tai ket noi don dap
static void
tcp_pcb_recycle_timewait_locked(void)
{
  struct tcp_pcb *tw = 0;
  for (int i = 0; i < TCP_PCB_POOL_SIZE; i++) {
    struct tcp_pcb *cand = &tcp_mgr.pcbs[i];
    acquire(&cand->lock);
    if (cand->state == TCP_TIME_WAIT && cand->sock == 0 &&
        cand->retransmit_q == 0 && cand->ooo_queue == 0) {
      if (!tw || (int32)(cand->rto_deadline - tw->rto_deadline) < 0)
        tw = cand;
    }
    release(&cand->lock);
  }
  if (tw) {
    acquire(&tw->lock);
    if (tw->state == TCP_TIME_WAIT && tw->sock == 0 &&
        tw->retransmit_q == 0 && tw->ooo_queue == 0) {
      tcp_pcb_detach_locked(tw);
      tw->state = TCP_CLOSED;
      tw->next = tcp_mgr.free_list;
      tcp_mgr.free_list = tw;
    }
    release(&tw->lock);
  }
}

// cap phat pcb moi
struct tcp_pcb *
tcp_pcb_alloc(void)
{
  struct tcp_pcb *pcb = 0;

  acquire(&tcp_mgr.lock);
  if (!tcp_mgr.free_list)
    tcp_pcb_recycle_timewait_locked();
  if (tcp_mgr.free_list) {
    pcb = tcp_mgr.free_list;
    tcp_mgr.free_list = pcb->next;
    pcb->next = 0;
    pcb->state = TCP_CLOSED;
    pcb->iss = (uint32)(ticks * 1000 + 1); // isn tinh theo clock ticks
    pcb->snd_nxt = pcb->iss;
    pcb->snd_una = pcb->iss;
    pcb->snd_wnd = 0;
    pcb->rcv_wnd = 4096;
    pcb->rcv_nxt = 0;
    pcb->irs = 0;
    pcb->srtt = 0;
    pcb->rttvar = 0;
    pcb->rto = 200; // rto khoi tao 200ms cho qemu lan
    pcb->backoff = 0;
    pcb->conn_err = 0;
    pcb->retransmit_q = 0;
    pcb->ooo_queue = 0;
    pcb->rto_deadline = 0;
    pcb->snd_wscale = 0;
    pcb->rcv_wscale = 0;
    pcb->wscale_ok = 0;
    pcb->syn_next = 0;
    tcp_cc_init(pcb);
    tcp_rate_init(pcb); // tuan 5: reset min_rtt / delivered / delivery_rate
    pcb->sock = 0;
    pcb->local_ip = 0;
    pcb->local_port = 0;
    pcb->remote_ip = 0;
    pcb->remote_port = 0;
  }
  release(&tcp_mgr.lock);

  return pcb;
}

// them pcb vao hash table khi da co du 4-tuple
void
tcp_pcb_insert(struct tcp_pcb *pcb)
{
  uint32 h = tcp_hash_fn(pcb->local_ip, pcb->local_port, pcb->remote_ip,
                         pcb->remote_port);
  acquire(&tcp_mgr.lock);
  pcb->next = tcp_mgr.hash_table[h];
  tcp_mgr.hash_table[h] = pcb;
  release(&tcp_mgr.lock);
}

// them vao listen list
void
tcp_pcb_set_listen(struct tcp_pcb *pcb)
{
  acquire(&tcp_mgr.lock);
  pcb->next = tcp_mgr.listen_list;
  tcp_mgr.listen_list = pcb;
  release(&tcp_mgr.lock);
}

// giai phong tcp_pcb
void
tcp_pcb_free(struct tcp_pcb *pcb)
{
  if (!pcb)
    return;

  // free retransmit queue
  struct mbuf *p = pcb->retransmit_q;
  while (p) {
    struct mbuf *next = p->next;
    p->next = 0;
    mbuffree(p);
    p = next;
  }
  pcb->retransmit_q = 0;

  // free ooo queue
  struct mbuf *q = pcb->ooo_queue;
  while (q) {
    struct mbuf *next = q->next;
    q->next = 0;
    mbuffree(q);
    q = next;
  }
  pcb->ooo_queue = 0;

  // neu pcb con nam trong syn_q cua listen socket thi go ra
  if (pcb->sock) {
    struct socket *ls = (struct socket *)pcb->sock;
    acquire(&ls->lock);
    struct tcp_pcb **curr = &ls->syn_q;
    while (*curr) {
      if (*curr == pcb) {
        *curr = pcb->syn_next;
        pcb->syn_next = 0;
        ls->syn_cnt--;
        break;
      }
      curr = &(*curr)->syn_next;
    }
    release(&ls->lock);
    pcb->sock = 0;
  }

  acquire(&tcp_mgr.lock);

  // chong double-free neu pcb da nam san trong free_list
  for (struct tcp_pcb *f = tcp_mgr.free_list; f; f = f->next) {
    if (f == pcb) {
      release(&tcp_mgr.lock);
      return;
    }
  }

  tcp_pcb_detach_locked(pcb);

  pcb->state = TCP_CLOSED;
  pcb->next = tcp_mgr.free_list;
  tcp_mgr.free_list = pcb;
  release(&tcp_mgr.lock);
}

// tim pcb theo 4-tuple hoac listen port
struct tcp_pcb *
tcp_lookup(uint32 src_ip, uint16 src_port, uint32 dst_ip, uint16 dst_port)
{
  struct tcp_pcb *pcb;

  // tim trong hash table truoc
  uint32 h = tcp_hash_fn(dst_ip, dst_port, src_ip, src_port);
  acquire(&tcp_mgr.lock);
  for (pcb = tcp_mgr.hash_table[h]; pcb; pcb = pcb->next) {
    if (pcb->local_ip == dst_ip && pcb->local_port == dst_port &&
        pcb->remote_ip == src_ip && pcb->remote_port == src_port) {
      release(&tcp_mgr.lock);
      return pcb;
    }
  }

  // tim pcb dang listen tren local_port
  for (pcb = tcp_mgr.listen_list; pcb; pcb = pcb->next) {
    if (pcb->state == TCP_LISTEN && pcb->local_port == dst_port) {
      release(&tcp_mgr.lock);
      return pcb;
    }
  }

  release(&tcp_mgr.lock);
  return 0;
}

// xu ly retransmission va time_wait dinh ky
void
tcp_timer_process(void)
{
  for (int i = 0; i < TCP_PCB_POOL_SIZE; i++) {
    struct tcp_pcb *pcb = &tcp_mgr.pcbs[i];
    if (pcb->state == TCP_CLOSED || pcb->state == TCP_LISTEN)
      continue;

    acquire(&pcb->lock);
    if (pcb->state == TCP_CLOSED || pcb->state == TCP_LISTEN) {
      release(&pcb->lock);
      continue;
    }

    // het han time_wait (2 MSL): thu hoi an toan duoi tcp_mgr.lock -> pcb->lock
    if (pcb->state == TCP_TIME_WAIT) {
      int expired = (pcb->rto_deadline > 0 &&
                     (int32)(ticks - pcb->rto_deadline) >= 0);
      release(&pcb->lock);
      if (expired) {
        acquire(&tcp_mgr.lock);
        acquire(&pcb->lock);
        if (pcb->state == TCP_TIME_WAIT && pcb->sock == 0 &&
            pcb->rto_deadline > 0 &&
            (int32)(ticks - pcb->rto_deadline) >= 0) {
          tcp_pcb_detach_locked(pcb);
          pcb->state = TCP_CLOSED;
          pcb->next = tcp_mgr.free_list;
          tcp_mgr.free_list = pcb;
        }
        release(&pcb->lock);
        release(&tcp_mgr.lock);
      }
      continue;
    }

    // kiem tra timeout retransmit
    if (pcb->retransmit_q && pcb->rto_deadline > 0 &&
        ticks >= pcb->rto_deadline) {
      struct mbuf *m = pcb->retransmit_q;

      // exponential backoff
      pcb->backoff++;
      m->retransmitted = 1; // karn: khong lay mau rtt tu segment nay
      pcb->rto = pcb->rto * 2;
      if (pcb->rto > RTO_MAX)
        pcb->rto = RTO_MAX;
      pcb->rto_deadline = ticks + (pcb->rto / 100 ? pcb->rto / 100 : 1);
      tcp_cc_on_timeout(pcb);

      if (pcb->backoff > MAX_BACKOFF) {
        // circuit breaker: huy ket noi, giai phong retransmit queue
        struct mbuf *p = pcb->retransmit_q;
        while (p) {
          struct mbuf *next = p->next;
          p->next = 0;
          mbuffree(p);
          p = next;
        }
        pcb->retransmit_q = 0;
        pcb->rto_deadline = 0;
        pcb->conn_err = ECONNRESET;
        if (pcb->state == TCP_SYN_RCVD && pcb->sock) {
          struct socket *ls = (struct socket *)pcb->sock;
          acquire(&ls->lock);
          struct tcp_pcb **curr = &ls->syn_q;
          while (*curr) {
            if (*curr == pcb) {
              *curr = pcb->syn_next;
              pcb->syn_next = 0;
              ls->syn_cnt--;
              break;
            }
            curr = &(*curr)->syn_next;
          }
          release(&ls->lock);
          pcb->sock = 0;
        }
        pcb->state = TCP_CLOSED;
        wakeup(pcb);
        if (pcb->sock) {
          sock_push_eof((struct socket *)pcb->sock);
          release(&pcb->lock);
        } else {
          // tuan 6 (TV1): pcb mo coi (socket da dong, vd. FIN_WAIT_1/LAST_ACK
          // mat lien tiep FIN) -> thu hoi luon, tranh pcb zombie
          release(&pcb->lock);
          tcp_pcb_free(pcb);
        }
        continue;
      }

      // truyen lai goi dau queue
      struct mbuf *copy = mbufalloc(MBUF_DEFAULT_HEAD);
      if (copy) {
        char *dst = mbufput(copy, m->len);
        if (dst) {
          memmove(dst, m->head, m->len);
          uint32 remote_ip = pcb->remote_ip;
          release(&pcb->lock);
          ip_output(copy, remote_ip, IP_PROTO_TCP);
          wakeup(pcb);
          continue;
        } else {
          mbuffree(copy);
        }
      }
    }
    release(&pcb->lock);
  }
}

// tuan 6 (TV1): kiem toan tang tcp cho bien ban zero-leak.
// dem mbuf con nam trong retransmit_q / ooo_queue cua moi pcb va phat hien
// pcb "zombie": state CLOSED, khong con socket nao tro toi (sock == 0) nhung
// lai khong nam trong free_list -> khong bao gio duoc thu hoi.
void
tcp_audit(struct net_audit *a)
{
  char in_free[TCP_PCB_POOL_SIZE];
  int nfree = 0, listen = 0, conns = 0, tw = 0, zombie = 0;
  int rtx = 0, ooo = 0;

  memset(in_free, 0, sizeof(in_free));

  acquire(&tcp_mgr.lock);
  for (struct tcp_pcb *p = tcp_mgr.free_list; p; p = p->next) {
    int idx = p - tcp_mgr.pcbs;
    if (idx < 0 || idx >= TCP_PCB_POOL_SIZE || in_free[idx])
      break; // free_list hong / co vong: dung de khong treo
    in_free[idx] = 1;
    nfree++;
  }

  for (int i = 0; i < TCP_PCB_POOL_SIZE; i++) {
    struct tcp_pcb *pcb = &tcp_mgr.pcbs[i];
    acquire(&pcb->lock);
    for (struct mbuf *m = pcb->retransmit_q; m; m = m->next)
      rtx++;
    for (struct mbuf *m = pcb->ooo_queue; m; m = m->next)
      ooo++;

    if (!in_free[i]) {
      if (pcb->state == TCP_LISTEN)
        listen++;
      else if (pcb->state == TCP_CLOSED) {
        if (pcb->sock == 0)
          zombie++;
      } else {
        conns++;
        if (pcb->state == TCP_TIME_WAIT)
          tw++;
      }
    }
    release(&pcb->lock);
  }
  release(&tcp_mgr.lock);

  a->tcp_pcb_total = TCP_PCB_POOL_SIZE;
  a->tcp_pcb_free = nfree;
  a->tcp_listen = listen;
  a->tcp_conns = conns;
  a->tcp_time_wait = tw;
  a->tcp_zombie = zombie;
  a->tcp_rtx_mbufs = rtx;
  a->tcp_ooo_mbufs = ooo;
}

// tong hop bien ban kiem toan toan bo ngan xep mang
void
net_audit_collect(struct net_audit *a, int flags)
{
  memset(a, 0, sizeof(*a));
  if (flags & NETAUDIT_FLUSH_ARP)
    arp_flush();

  // thu tu: cac tang giu mbuf truoc, pool sau cung de giam sai lech khi
  // co goi dang di chuyen giua cac tang trong luc chup
  tcp_audit(a);
  arp_audit(a);
  e1000_audit(a);
  mbuf_audit(a);

  a->accounted = a->mbuf_free + a->rx_ring_held + a->tx_inflight +
                 a->arp_pending_mbufs + a->tcp_rtx_mbufs + a->tcp_ooo_mbufs;
  a->leaked = a->mbuf_total - a->accounted;
}

// lay thong ke cac ket noi tcp dang hoat dong vao buffer
int
tcp_get_stats(struct tcp_stat *stats, int max_entries)
{
  if (!stats || max_entries <= 0)
    return 0;

  int count = 0;
  acquire(&tcp_mgr.lock);
  for (int i = 0; i < TCP_PCB_POOL_SIZE && count < max_entries; i++) {
    struct tcp_pcb *pcb = &tcp_mgr.pcbs[i];
    if (pcb->state == TCP_CLOSED)
      continue;

    acquire(&pcb->lock);
    stats[count].local_ip = pcb->local_ip;
    stats[count].local_port = pcb->local_port;
    stats[count].remote_ip = pcb->remote_ip;
    stats[count].remote_port = pcb->remote_port;
    stats[count].state = (int)pcb->state;
    stats[count].cwnd = pcb->cwnd;
    stats[count].ssthresh = pcb->ssthresh;
    stats[count].rtt = (pcb->srtt >> 3);
    stats[count].rto = pcb->rto;
    stats[count].snd_una = pcb->snd_una;
    stats[count].snd_nxt = pcb->snd_nxt;
    stats[count].snd_wnd = pcb->snd_wnd;
    stats[count].rcv_wnd = pcb->rcv_wnd;
    stats[count].dupack_cnt = pcb->dupack_cnt;
    stats[count].backoff = pcb->backoff;
    stats[count].rtt_us = pcb->latest_rtt_us;
    stats[count].min_rtt_us = pcb->min_rtt_us;
    stats[count].delivery_rate = pcb->delivery_rate;
    stats[count].cc_algo = pcb->cc_algo;
    stats[count].bbr_state = pcb->bbr_state;
    stats[count].max_bw = pcb->bbr_max_bw;
    stats[count].bdp = pcb->bbr_bdp;
    release(&pcb->lock);

    count++;
  }
  release(&tcp_mgr.lock);

  return count;
}

// in danh sach va trang thai cac ket noi tcp ra console kernel
void
tcp_dump_stats(void)
{
  static const char *st_names[] = {
    "CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD",
    "ESTAB", "FIN_WAIT_1", "FIN_WAIT_2",
    "CLOSE_WAIT", "CLOSING", "LAST_ACK", "TIME_WAIT"
  };

  acquire(&tcp_mgr.lock);
  for (int i = 0; i < TCP_PCB_POOL_SIZE; i++) {
    struct tcp_pcb *pcb = &tcp_mgr.pcbs[i];
    if (pcb->state == TCP_CLOSED)
      continue;

    acquire(&pcb->lock);
    const char *st = (pcb->state >= 0 && pcb->state <= TCP_TIME_WAIT)
                     ? st_names[pcb->state] : "???";

    printk("tcp[%d]: %s %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d cwnd=%d ssthresh=%d rtt=%d rto=%d\n",
           i, st,
           (pcb->local_ip >> 24) & 0xff, (pcb->local_ip >> 16) & 0xff,
           (pcb->local_ip >> 8) & 0xff, pcb->local_ip & 0xff, pcb->local_port,
           (pcb->remote_ip >> 24) & 0xff, (pcb->remote_ip >> 16) & 0xff,
           (pcb->remote_ip >> 8) & 0xff, pcb->remote_ip & 0xff, pcb->remote_port,
           pcb->cwnd, pcb->ssthresh, pcb->srtt >> 3, pcb->rto);
    const char *cc_str = (pcb->cc_algo == CC_BBR) ? "BBR" :
                         (pcb->cc_algo == CC_BASIC) ? "Basic" : "Reno";
    printk("tcp[%d]:   cc=%s(%d) bbr_st=%d bdp=%d max_bw=%d rtt_us=%d min_rtt_us=%d delivered=%lu rate=%dB/s samples=%d\n",
           i, cc_str, pcb->cc_algo,
           pcb->bbr_state, pcb->bbr_bdp, pcb->bbr_max_bw,
           pcb->latest_rtt_us, pcb->min_rtt_us, pcb->delivered,
           pcb->delivery_rate, pcb->rtt_samples);
    release(&pcb->lock);
  }
  release(&tcp_mgr.lock);
}

// cap nhat thuat toan kiem soat tac nghen cho tat ca cac ket noi dang hoat dong
void
tcp_pcb_update_cc(int algo)
{
  acquire(&tcp_mgr.lock);
  for (int i = 0; i < TCP_PCB_POOL_SIZE; i++) {
    struct tcp_pcb *pcb = &tcp_mgr.pcbs[i];
    if (pcb->state != TCP_CLOSED) {
      acquire(&pcb->lock);
      pcb->cc_algo = algo;
      release(&pcb->lock);
    }
  }
  release(&tcp_mgr.lock);
}
