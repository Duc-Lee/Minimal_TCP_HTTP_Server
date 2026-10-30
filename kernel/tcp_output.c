// dong goi tcp va truyen xuong ip

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// clone mbuf de giu lai trong queue truyen lai phong khi mat goi
static struct mbuf *
mbuf_clone_for_retransmit(struct mbuf *m, uint32 seq_end)
{
  struct mbuf *clone = mbufalloc(MBUF_DEFAULT_HEAD);
  if (!clone)
    return 0;

  char *dst = mbufput(clone, m->len);
  if (!dst) {
    mbuffree(clone);
    return 0;
  }
  memmove(dst, m->head, m->len);

  clone->seq_end = seq_end;
  return clone;
}

// tao header tcp, tinh checksum va gui xuong tang ip
int
tcp_output(struct tcp_pcb *pcb, struct mbuf *m, uint8 flags)
{
  struct tcphdr *th;
  int datalen;

  if (!m) {
    m = mbufalloc(MBUF_DEFAULT_HEAD);
    if (!m)
      return -1;
  }

  datalen = (int)m->len;

  th = (struct tcphdr *)mbufpush(m, sizeof(struct tcphdr));
  if (!th) {
    mbuffree(m);
    return -1;
  }

  th->sport = htons(pcb->local_port);
  th->dport = htons(pcb->remote_port);
  th->seq = htonl(pcb->snd_nxt);
  th->ack = htonl(pcb->rcv_nxt);
  th->off = (sizeof(struct tcphdr) / 4) << 4;
  th->flags = flags;
  th->win = htons(pcb->rcv_wnd);
  th->urp = 0;
  th->cksum = 0;

  // checksum tcp voi pseudo header
  th->cksum = tcp_checksum(m, pcb->local_ip, pcb->remote_ip);

  uint32 seq_start = pcb->snd_nxt;
  uint32 seq_end = seq_start;

  if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))
    seq_end++;
  else if (datalen > 0)
    seq_end += datalen;

  pcb->snd_nxt = seq_end;

  // luu ban sao vao retransmit_q neu co data hoac syn/fin (ack thuan khong can)
  if (datalen > 0 || (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))) {
    struct mbuf *clone = mbuf_clone_for_retransmit(m, seq_end);
    if (clone) {
      acquire(&pcb->lock);
      struct mbuf **tail = &pcb->retransmit_q;
      while (*tail)
        tail = &(*tail)->next;
      *tail = clone;

      if (pcb->rto_deadline == 0)
        pcb->rto_deadline = ticks + (pcb->rto / 100 ? pcb->rto / 100 : 1);
      release(&pcb->lock);
    }
  }

  return ip_output(m, pcb->remote_ip, IP_PROTO_TCP);
}

// phan hoi goi tin co co RST theo chuan RFC 793 khi nhan segment tai cong dong
void
tcp_send_rst(uint32 local_ip, uint16 local_port, uint32 remote_ip,
             uint16 remote_port, uint32 in_seq, uint32 in_ack,
             uint8 in_flags, int in_datalen)
{
  if (in_flags & TCP_FLAG_RST)
    return;

  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEAD);
  if (!m)
    return;

  struct tcphdr *th = (struct tcphdr *)mbufpush(m, sizeof(struct tcphdr));
  if (!th) {
    mbuffree(m);
    return;
  }

  memset(th, 0, sizeof(struct tcphdr));
  th->sport = htons(local_port);
  th->dport = htons(remote_port);
  th->off = (sizeof(struct tcphdr) / 4) << 4;
  th->win = 0;
  th->urp = 0;

  // Theo RFC 793 Page 36 (Reset Generation):
  if (in_flags & TCP_FLAG_ACK) {
    th->seq = htonl(in_ack);
    th->ack = 0;
    th->flags = TCP_FLAG_RST;
  } else {
    th->seq = 0;
    uint32 ack_num = in_seq + in_datalen;
    if (in_flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))
      ack_num++;
    th->ack = htonl(ack_num);
    th->flags = TCP_FLAG_RST | TCP_FLAG_ACK;
  }

  th->cksum = 0;
  th->cksum = tcp_checksum(m, local_ip, remote_ip);

  ip_output(m, remote_ip, IP_PROTO_TCP);
}
