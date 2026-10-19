// ============================================================================
// Thành viên 2 (TV2): Lõi Giao vận TCP, RTO Timer
// File: kernel/tcp_input.c
// Nhiệm vụ: Xử lý gói TCP nhận vào — FSM 11 trạng thái, Sliding Window, ACK
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

// Forward declaration: sock_notify_readable báo cho socket biết có data mới
static inline void
sock_push_data(struct tcp_pcb *pcb, struct mbuf *m)
{
  struct socket *s = (struct socket *)pcb->sock;
  if (!s || m->len == 0) {
    mbuffree(m);
    return;
  }

  // Đẩy dữ liệu vào rx_buf của socket
  ringbuf_write(&s->rx_buf, m->head, m->len);
  // Đánh thức process đang chờ đọc
  wakeup(&s->rx_buf);
  mbuffree(m);
}

// Xử lý ACK, giải phóng retransmit_q, Fast Retransmit và đánh thức socket writer
static void
tcp_handle_ack(struct tcp_pcb *pcb, uint32 ack, uint16 win)
{
  pcb->snd_wnd = win;
  if ((int32)(ack - pcb->snd_una) > 0) {
    // Giải phóng các segment đã được ACK khỏi retransmit_q
    struct mbuf **pq = &pcb->retransmit_q;
    while (*pq) {
      uint32 seg_end = *(uint32 *)((*pq)->buf);
      if ((int32)(ack - seg_end) >= 0) {
        struct mbuf *done = *pq;
        *pq = done->next;
        done->next = 0;
        mbuffree(done);
      } else {
        pq = &(*pq)->next;
      }
    }
    pcb->snd_una  = ack;
    pcb->dup_acks = 0;
    pcb->backoff  = 0;
    pcb->rto      = 200; // Reset về giá trị baseline RTO
    if (pcb->retransmit_q) {
      uint32 t = pcb->rto / 10;
      pcb->rto_deadline = ticks + (t ? t : 1);
    } else {
      pcb->rto_deadline = 0;
    }
    wakeup(pcb); // Đánh thức sock_write đang chờ sliding window
  } else if (ack == pcb->snd_una && pcb->retransmit_q != 0) {
    pcb->dup_acks++;
    if (pcb->dup_acks == 3) {
      // Fast Retransmit (RFC 5681): truyền lại ngay segment đầu tiên trong hàng đợi
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
      uint32 t = pcb->rto / 10;
      pcb->rto_deadline = ticks + (t ? t : 1);
    }
  }
}

// ─── ĐIỂM GIAO TIẾP 1: TV1 → TV2 (Khi có gói TCP từ card mạng lên) ─────────
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

  // Kiểm tra Checksum TCP
  if (ck != 0) {
    mbuffree(m);
    return;
  }
  uint16 sport = ntohs(th->sport);
  uint16 dport = ntohs(th->dport);
  uint32 seq   = ntohl(th->seq);
  uint32 ack   = ntohl(th->ack);
  uint8  flags = th->flags;
  uint16 win   = ntohs(th->win);

  int hlen = (th->off >> 4) * 4;
  if (hlen < (int)sizeof(struct tcphdr)) {
    mbuffree(m);
    return;
  }
  mbufpull(m, hlen); // Dữ liệu còn lại trong mbuf là TCP Payload

  // Tra cứu PCB quản lý kết nối này
  struct tcp_pcb *pcb = tcp_lookup(src_ip, sport, dst_ip, dport);
  if (!pcb) {
    // TODO: Gửi TCP RST
    mbuffree(m);
    return;
  }

  acquire(&pcb->lock);

  switch (pcb->state) {

  // ── LISTEN: Chờ kết nối đến ────────────────────────────────────────────────
  case TCP_LISTEN:
    if (flags & TCP_FLAG_SYN) {
      // Clone PCB mới cho kết nối này, để PCB listen tiếp tục nhận SYN khác
      struct tcp_pcb *newpcb = tcp_pcb_alloc();
      if (!newpcb) {
        release(&pcb->lock);
        mbuffree(m);
        return;
      }
      newpcb->local_ip   = dst_ip;
      newpcb->local_port = dport;
      newpcb->remote_ip  = src_ip;
      newpcb->remote_port = sport;
      newpcb->irs        = seq;
      newpcb->rcv_nxt    = seq + 1;
      newpcb->rcv_wnd    = 4096;
      newpcb->state      = TCP_SYN_RCVD;
      newpcb->sock       = pcb->sock; // Chia sẻ socket listen để accept() lấy ra

      // Đăng ký newpcb vào hash table (có 4-tuple đầy đủ)
      tcp_pcb_insert(newpcb);

      release(&pcb->lock);
      mbuffree(m);

      // Gửi SYN-ACK hoàn tất bước 2 của 3-way handshake
      tcp_output(newpcb, 0, TCP_FLAG_SYN | TCP_FLAG_ACK);
      return;
    }
    break;

  // ── SYN_SENT: Client đã gửi SYN, chờ SYN-ACK ──────────────────────────────
  case TCP_SYN_SENT:
    if ((flags & TCP_FLAG_SYN) && (flags & TCP_FLAG_ACK)) {
      pcb->irs      = seq;
      pcb->rcv_nxt  = seq + 1;
      pcb->snd_una  = ack;
      pcb->snd_wnd  = win;
      pcb->state    = TCP_ESTABLISHED;
      release(&pcb->lock);
      mbuffree(m);
      // Gửi ACK hoàn tất bắt tay 3 bước (bước 3)
      tcp_output(pcb, 0, TCP_FLAG_ACK);
      return;
    }
    break;

  // ── SYN_RCVD: Server đã gửi SYN-ACK, chờ ACK ──────────────────────────────
  case TCP_SYN_RCVD:
    if (flags & TCP_FLAG_SYN) {
      // Client truyền lại SYN (do SYN-ACK trước đó bị drop): truyền lại SYN-ACK
      release(&pcb->lock);
      mbuffree(m);
      tcp_output(pcb, 0, TCP_FLAG_SYN | TCP_FLAG_ACK);
      return;
    }
    if (flags & TCP_FLAG_ACK) {
      pcb->snd_una = ack;
      pcb->snd_wnd = win;
      pcb->state   = TCP_ESTABLISHED;

      // Đẩy newpcb vào backlog của socket LISTEN để sock_accept() lấy ra
      struct socket *ls = (struct socket *)pcb->sock;
      if (ls) {
        acquire(&ls->lock);
        if (ls->backlog_cnt < ls->backlog_max) {
          // Cấp socket mới cho kết nối vừa được chấp nhận
          struct socket *newsock = sock_alloc_for_pcb(pcb);
          if (newsock) {
            newsock->next  = ls->backlog_q;
            ls->backlog_q  = newsock;
            ls->backlog_cnt++;
            pcb->sock = newsock;
          }
        }
        release(&ls->lock);
        wakeup(ls); // Đánh thức sock_accept() đang ngủ
      }
    }
    break;

  // ── ESTABLISHED: Kết nối đang hoạt động ─────────────────────────────────────
  case TCP_ESTABLISHED:
    if (flags & TCP_FLAG_ACK) {
      tcp_handle_ack(pcb, ack, win);
    }

    // Xử lý dữ liệu nhận (Sliding Window in-order)
    if (m->len > 0) {
      if (seq == pcb->rcv_nxt) {
        pcb->rcv_nxt += m->len;
        // Đẩy payload vào socket rx_buf và gửi ACK
        release(&pcb->lock);
        sock_push_data(pcb, m);
        tcp_output(pcb, 0, TCP_FLAG_ACK);
        return;
      }
      // Out-of-order: bỏ gói (Phase 1 chỉ in-order, TCP retransmit sẽ gửi lại)
    }

    // Xử lý FIN từ đối phương
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

  // ── FIN_WAIT_1: Đã gửi FIN, chờ ACK ─────────────────────────────────────────
  case TCP_FIN_WAIT_1:
    if (flags & TCP_FLAG_ACK) {
      pcb->snd_una = ack;
      if (flags & TCP_FLAG_FIN) {
        // Simultaneous close: FIN+ACK cùng lúc
        pcb->rcv_nxt++;
        pcb->state = TCP_TIME_WAIT;
        pcb->rto_deadline = ticks + 200; // 2*MSL ≈ 2 giây @ 100Hz
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

  // ── FIN_WAIT_2: ACK cho FIN của ta đã đến, chờ FIN từ remote ────────────────
  case TCP_FIN_WAIT_2:
    if (flags & TCP_FLAG_FIN) {
      pcb->rcv_nxt++;
      pcb->state = TCP_TIME_WAIT;
      pcb->rto_deadline = ticks + 200; // 2*MSL timer
      if (pcb->sock)
        sock_push_eof((struct socket *)pcb->sock);
      release(&pcb->lock);
      mbuffree(m);
      tcp_output(pcb, 0, TCP_FLAG_ACK);
      return;
    }
    break;

  // ── CLOSE_WAIT: Nhận FIN từ remote, ứng dụng chưa đóng ──────────────────────
  case TCP_CLOSE_WAIT:
    if (flags & TCP_FLAG_ACK) {
      tcp_handle_ack(pcb, ack, win);
    }
    // Ứng dụng sẽ gọi close() → sock_close() → gửi FIN → TCP_LAST_ACK
    break;

  // ── LAST_ACK: Đã gửi FIN, chờ ACK cuối ─────────────────────────────────────
  case TCP_LAST_ACK:
    if (flags & TCP_FLAG_ACK) {
      pcb->state = TCP_CLOSED;
      release(&pcb->lock);
      mbuffree(m);
      tcp_pcb_free(pcb);
      return;
    }
    break;

  // ── CLOSING: Simultaneous close ─────────────────────────────────────────────
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
