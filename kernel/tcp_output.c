// ============================================================================
// Thành viên 2 (TV2): Lõi Giao vận TCP, RTO Timer
// File: kernel/tcp_output.c
// Nhiệm vụ: Đóng gói TCP Header, kiểm tra Send Window và gửi qua IP
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// Clone một mbuf để lưu vào retransmit_q
// Lưu seq_end vào 4 byte đầu của buf[] (vùng headroom trước head)
static struct mbuf*
mbuf_clone_for_retransmit(struct mbuf *m, uint32 seq_end)
{
  struct mbuf *clone = mbufalloc(MBUF_DEFAULT_HEAD);
  if (!clone)
    return 0;

  // Copy payload từ m vào clone
  char *dst = mbufput(clone, m->len);
  if (!dst) {
    mbuffree(clone);
    return 0;
  }
  memmove(dst, m->head, m->len);

  // Lưu seq_end vào vùng trước head (headroom)
  *(uint32 *)(clone->buf) = seq_end;

  return clone;
}

// ─── tcp_output: Đóng gói và gửi một TCP segment ────────────────────────────
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

  datalen = (int)m->len; // Chiều dài payload trước khi push header

  th = (struct tcphdr *)mbufpush(m, sizeof(struct tcphdr));
  if (!th) {
    mbuffree(m);
    return -1;
  }

  th->sport = htons(pcb->local_port);
  th->dport = htons(pcb->remote_port);
  th->seq   = htonl(pcb->snd_nxt);
  th->ack   = htonl(pcb->rcv_nxt);
  th->off   = (sizeof(struct tcphdr) / 4) << 4;
  th->flags = flags;
  th->win   = htons(pcb->rcv_wnd);
  th->urp   = 0;
  th->cksum = 0;

  // Tính TCP checksum kèm Pseudo Header
  th->cksum = tcp_checksum(m, pcb->local_ip, pcb->remote_ip);

  // Tính seq_end cho retransmit tracking
  uint32 seq_start = pcb->snd_nxt;
  uint32 seq_end   = seq_start;

  if (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))
    seq_end++;        // SYN và FIN chiếm 1 sequence number
  else if (datalen > 0)
    seq_end += datalen;

  // Cập nhật snd_nxt
  pcb->snd_nxt = seq_end;

  // Lưu bản sao vào retransmit_q nếu gói có payload hoặc cờ SYN/FIN
  // (ACK thuần túy không cần retransmit)
  if (datalen > 0 || (flags & (TCP_FLAG_SYN | TCP_FLAG_FIN))) {
    struct mbuf *clone = mbuf_clone_for_retransmit(m, seq_end);
    if (clone) {
      acquire(&pcb->lock);
      // Nối vào cuối retransmit_q
      struct mbuf **tail = &pcb->retransmit_q;
      while (*tail)
        tail = &(*tail)->next;
      *tail = clone;

      // Đặt deadline nếu chưa có
      if (pcb->rto_deadline == 0)
        pcb->rto_deadline = ticks + pcb->rto / (1000 / 100);
      release(&pcb->lock);
    }
  }

  // ── ĐIỂM GIAO TIẾP 2: TV2 → TV1 truyền segment TCP xuống IP ─────────────────
  return ip_output(m, pcb->remote_ip, IP_PROTO_TCP);
}
