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
  clone->retransmitted = 0;
  clone->send_tick = ticks;
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

  int optlen = 0;
  int include_wscale = 0;
  if (flags & TCP_FLAG_SYN) {
    if (flags & TCP_FLAG_ACK) {
      // SYN-ACK: chi gui wscale neu peer da de nghi trong SYN
      include_wscale = pcb->wscale_ok;
    } else {
      // SYN chu dong: luon de nghi wscale
      include_wscale = 1;
    }
    optlen = include_wscale ? 8 : 4;
  }

  int tcphdr_len = sizeof(struct tcphdr) + optlen;
  th = (struct tcphdr *)mbufpush(m, tcphdr_len);
  if (!th) {
    mbuffree(m);
    return -1;
  }

  memset(th, 0, tcphdr_len);
  th->sport = htons(pcb->local_port);
  th->dport = htons(pcb->remote_port);
  th->seq = htonl(pcb->snd_nxt);
  th->ack = htonl(pcb->rcv_nxt);
  th->off = (tcphdr_len / 4) << 4;
  th->flags = flags;

  // Window advertisement:
  // Theo RFC 7323: trong goi SYN (SYN hoac SYN-ACK) window field KHONG duoc phep scale
  if (flags & TCP_FLAG_SYN) {
    uint32 w = pcb->rcv_wnd;
    if (w > 65535)
      w = 65535;
    th->win = htons((uint16)w);

    uint8 *opt = (uint8 *)(th + 1);
    opt[0] = TCPOPT_MSS;
    opt[1] = 4;
    opt[2] = (TCP_MSS >> 8) & 0xff;
    opt[3] = TCP_MSS & 0xff;
    if (include_wscale) {
      opt[4] = TCPOPT_NOP;
      opt[5] = TCPOPT_WSCALE;
      opt[6] = 3;
      opt[7] = TCP_RCV_WSCALE;
    }
  } else {
    if (pcb->wscale_ok) {
      uint32 scaled_win = pcb->rcv_wnd >> pcb->rcv_wscale;
      if (scaled_win > 65535)
        scaled_win = 65535;
      th->win = htons((uint16)scaled_win);
    } else {
      uint32 w = pcb->rcv_wnd;
      if (w > 65535)
        w = 65535;
      th->win = htons((uint16)w);
    }
  }

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
    if (!clone) {
      pcb->snd_nxt = seq_start;
      mbuffree(m);
      return -1;
    }
    acquire(&pcb->lock);
    struct mbuf **tail = &pcb->retransmit_q;
    while (*tail)
      tail = &(*tail)->next;
    *tail = clone;

    if (pcb->rto_deadline == 0)
      pcb->rto_deadline = ticks + (pcb->rto / 100 ? pcb->rto / 100 : 1);
    release(&pcb->lock);
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
