// xu ly segment tcp den
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// day payload vao rx_buf cua socket
static inline void
sock_push_data(struct tcp_pcb *pcb, struct mbuf *m)
{
  struct socket *s = (struct socket *)pcb->sock;
  if (!s || m->len == 0) {
    mbuffree(m);
    return;
  }

  ringbuf_write(&s->rx_buf, m->head, m->len);
  wakeup(&s->rx_buf);
  mbuffree(m);
}

// xu ly ack, don retransmit_q, fast retransmit va wakeup writer
static void
tcp_handle_ack(struct tcp_pcb *pcb, uint32 ack, uint16 win)
{
  pcb->snd_wnd = win;
  if ((int32)(ack - pcb->snd_una) > 0) {
    // giai phong segment da duoc ack
    struct mbuf **pq = &pcb->retransmit_q;
    while (*pq) {
      uint32 seg_end = (*pq)->seq_end;
      if ((int32)(ack - seg_end) >= 0) {
        struct mbuf *done = *pq;
        *pq = done->next;
        done->next = 0;
        mbuffree(done);
      } else {
        pq = &(*pq)->next;
      }
    }
    pcb->snd_una = ack;
    pcb->dup_acks = 0;
    pcb->backoff = 0;
    pcb->rto = 200; // reset ve baseline rto
    if (pcb->retransmit_q) {
      uint32 t = pcb->rto / 100;
      pcb->rto_deadline = ticks + (t ? t : 1);
    } else {
      pcb->rto_deadline = 0;
    }
    wakeup(pcb);
  } else if (ack == pcb->snd_una && pcb->retransmit_q != 0) {
    pcb->dup_acks++;
    if (pcb->dup_acks == 3) {
      // fast retransmit khi nhan du 3 dup acks
      struct mbuf *m = pcb->retransmit_q;
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
  }
}

// diem vao xu ly segment tcp tu ip layer
void
tcp_input(struct mbuf *m, uint32 src_ip, uint32 dst_ip)
{
  struct tcphdr *th;

  if (m->len < sizeof(struct tcphdr)) {
    mbuffree(m);
    return;
  }

  th = (struct tcphdr *)m->head;
  uint16 ck = tcp_checksum(m, src_ip, dst_ip);

  // kiem tra checksum tcp
  if (ck != 0) {
    mbuffree(m);
    return;
  }
  uint16 sport = ntohs(th->sport);
  uint16 dport = ntohs(th->dport);
  uint32 seq = ntohl(th->seq);
  uint32 ack = ntohl(th->ack);
  uint8 flags = th->flags;
  uint16 win = ntohs(th->win);

  int hlen = (th->off >> 4) * 4;
  if (hlen < (int)sizeof(struct tcphdr)) {
    mbuffree(m);
    return;
  }
  // bo tcp header lay payload
  mbufpull(m, hlen);

  struct tcp_pcb *pcb = tcp_lookup(src_ip, sport, dst_ip, dport);
  if (!pcb) {
    mbuffree(m);
    return;
  }

  acquire(&pcb->lock);

  // xu ly co RST: client huy hoac dong ket noi dot ngot
  if (flags & TCP_FLAG_RST) {
    if (pcb->state != TCP_LISTEN && pcb->state != TCP_CLOSED) {
      pcb->state = TCP_CLOSED;
      wakeup(pcb);
      if (pcb->sock)
        sock_push_eof((struct socket *)pcb->sock);
    }
    release(&pcb->lock);
    mbuffree(m);
    return;
  }

  switch (pcb->state) {

  case TCP_LISTEN:
    if (flags & TCP_FLAG_SYN) {
      // tao pcb moi cho ket noi den
      struct tcp_pcb *newpcb = tcp_pcb_alloc();
      if (!newpcb) {
        release(&pcb->lock);
        mbuffree(m);
        return;
      }
      newpcb->local_ip = dst_ip;
      newpcb->local_port = dport;
      newpcb->remote_ip = src_ip;
      newpcb->remote_port = sport;
      newpcb->irs = seq;
      newpcb->rcv_nxt = seq + 1;
      newpcb->rcv_wnd = 4096;
      newpcb->state = TCP_SYN_RCVD;
      newpcb->sock = pcb->sock;

      tcp_pcb_insert(newpcb);

      release(&pcb->lock);
      mbuffree(m);

      // gui syn-ack buoc 2
      tcp_output(newpcb, 0, TCP_FLAG_SYN | TCP_FLAG_ACK);
      return;
    }
    break;

  case TCP_SYN_SENT:
    if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
      pcb->irs = seq;
      pcb->rcv_nxt = seq + 1;
      pcb->snd_una = ack;
      pcb->snd_wnd = win;
      pcb->state = TCP_ESTABLISHED;
      release(&pcb->lock);
      mbuffree(m);
      // gui ack buoc 3
      tcp_output(pcb, 0, TCP_FLAG_ACK);
      return;
    }
    break;

  case TCP_SYN_RCVD:
    if (flags & TCP_FLAG_SYN) {
      // client retransmit syn do rot syn-ack, gui lai syn-ack
      release(&pcb->lock);
      mbuffree(m);
      tcp_output(pcb, 0, TCP_FLAG_SYN | TCP_FLAG_ACK);
      return;
    }
    if (flags & TCP_FLAG_ACK) {
      pcb->snd_una = ack;
      pcb->snd_wnd = win;
      pcb->state = TCP_ESTABLISHED;

      // day vao backlog cua socket listen de accept lay ra
      struct socket *ls = (struct socket *)pcb->sock;
      if (ls) {
        acquire(&ls->lock);
        if (ls->backlog_cnt < ls->backlog_max) {
          struct socket *newsock = sock_alloc_for_pcb(pcb);
          if (newsock) {
            newsock->next = ls->backlog_q;
            ls->backlog_q = newsock;
            ls->backlog_cnt++;
            pcb->sock = newsock;
          }
        }
        release(&ls->lock);
        wakeup(ls);
      }
    }
    break;

  case TCP_ESTABLISHED:
    if (flags & TCP_FLAG_ACK) {
      tcp_handle_ack(pcb, ack, win);
    }

    // nhan payload in-order
    if (m->len > 0) {
      if (seq == pcb->rcv_nxt) {
        pcb->rcv_nxt += m->len;
        release(&pcb->lock);
        sock_push_data(pcb, m);
        tcp_output(pcb, 0, TCP_FLAG_ACK);
        return;
      }
      // out of order thi bo qua de doi retransmit
    }

    // remote dong chieu gui
    if (flags & TCP_FLAG_FIN) {
      pcb->rcv_nxt++;
      pcb->state = TCP_CLOSE_WAIT;
      if (pcb->sock)
        sock_push_eof((struct socket *)pcb->sock);
      release(&pcb->lock);
      mbuffree(m);
      tcp_output(pcb, 0, TCP_FLAG_ACK);
      return;
    }
    break;

  case TCP_FIN_WAIT_1:
    if (flags & TCP_FLAG_ACK) {
      pcb->snd_una = ack;
      if (flags & TCP_FLAG_FIN) {
        pcb->rcv_nxt++;
        pcb->state = TCP_TIME_WAIT;
        pcb->rto_deadline = ticks + 200;
        if (pcb->sock)
          sock_push_eof((struct socket *)pcb->sock);
        release(&pcb->lock);
        mbuffree(m);
        tcp_output(pcb, 0, TCP_FLAG_ACK);
        return;
      }
      pcb->state = TCP_FIN_WAIT_2;
    }
    break;

  // fin_wait_2: da nhan ack cho fin cua minh, cho fin tu remote
  case TCP_FIN_WAIT_2:
    if (flags & TCP_FLAG_FIN) {
      pcb->rcv_nxt++;
      pcb->state = TCP_TIME_WAIT;
      pcb->rto_deadline = ticks + 200; // time_wait ~2 msl
      if (pcb->sock)
        sock_push_eof((struct socket *)pcb->sock);
      release(&pcb->lock);
      mbuffree(m);
      tcp_output(pcb, 0, TCP_FLAG_ACK);
      return;
    }
    break;

  // close_wait: remote gui fin truoc, cho app dong socket de gui fin lai
  case TCP_CLOSE_WAIT:
    if (flags & TCP_FLAG_ACK) {
      tcp_handle_ack(pcb, ack, win);
    }
    break;

  // last_ack: da gui fin, cho ack cuoi de dong han
  case TCP_LAST_ACK:
    if (flags & TCP_FLAG_ACK) {
      pcb->state = TCP_CLOSED;
      release(&pcb->lock);
      mbuffree(m);
      tcp_pcb_free(pcb);
      return;
    }
    break;

  // closing: ca 2 ben dong cung luc
  case TCP_CLOSING:
    if (flags & TCP_FLAG_ACK) {
      pcb->state = TCP_TIME_WAIT;
      pcb->rto_deadline = ticks + 200;
    }
    break;

  default:
    break;
  }

  release(&pcb->lock);
  mbuffree(m);
}
