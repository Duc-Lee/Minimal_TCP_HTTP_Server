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
}

// cap phat pcb moi
struct tcp_pcb *
tcp_pcb_alloc(void)
{
  struct tcp_pcb *pcb = 0;

  acquire(&tcp_mgr.lock);
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

  // xoa khoi hash table
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

  // xoa khoi listen list
  curr = &tcp_mgr.listen_list;
  while (*curr) {
    if (*curr == pcb) {
      *curr = pcb->next;
      break;
    }
    curr = &(*curr)->next;
  }

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

    // het han time_wait
    if (pcb->state == TCP_TIME_WAIT) {
      if (pcb->rto_deadline > 0 && ticks >= pcb->rto_deadline) {
        release(&pcb->lock);
        tcp_pcb_free(pcb);
        continue;
      }
      release(&pcb->lock);
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
        if (pcb->sock)
          sock_push_eof((struct socket *)pcb->sock);
        release(&pcb->lock);
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
    release(&pcb->lock);
  }
  release(&tcp_mgr.lock);
}
