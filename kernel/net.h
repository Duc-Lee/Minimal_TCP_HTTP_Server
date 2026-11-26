#ifndef _NET_H_
#define _NET_H_

#include "types.h"
#include "spinlock.h"

// macro byte order network <-> host
static inline uint16
bswap16(uint16 x)
{
  return ((x & 0x00ff) << 8) | ((x & 0xff00) >> 8);
}

static inline uint32
bswap32(uint32 x)
{
  return ((x & 0x000000ff) << 24) | ((x & 0x0000ff00) << 8) |
         ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24);
}

#define htons(x) bswap16(x)
#define ntohs(x) bswap16(x)
#define htonl(x) bswap32(x)
#define ntohl(x) bswap32(x)

#define MAKE_IP_ADDR(a, b, c, d)                                               \
  (((uint32)(a) << 24) | ((uint32)(b) << 16) | ((uint32)(c) << 8) | (uint32)(d))

// hang so protocol
#define ETH_ADDR_LEN 6
#define ETH_TYPE_IP  0x0800
#define ETH_TYPE_ARP 0x0806

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

// ethernet header l2
struct eth {
  uint8 dhost[ETH_ADDR_LEN];
  uint8 shost[ETH_ADDR_LEN];
  uint16 type;
} __attribute__((packed));

// arp packet format
struct arp {
  uint16 hrd;              // hardware type (1 = ethernet)
  uint16 pro;              // protocol type (0x0800 = ipv4)
  uint8 hln;               // mac len (6)
  uint8 pln;               // ip len (4)
  uint16 op;               // opcode (1: request, 2: reply)
  uint8 sha[ETH_ADDR_LEN]; // sender mac
  uint32 sip;              // sender ip
  uint8 tha[ETH_ADDR_LEN]; // target mac
  uint32 tip;              // target ip
} __attribute__((packed));

// ipv4 header l3
struct ip {
  uint8 vihl;   // version (4 bit) + ihl (4 bit)
  uint8 tos;    // type of service
  uint16 len;   // total length
  uint16 id;    // identification
  uint16 off;   // flags va fragment offset
  uint8 ttl;    // time to live
  uint8 proto;  // protocol (6 = tcp)
  uint16 cksum; // header checksum
  uint32 src;   // source ip
  uint32 dst;   // dest ip
} __attribute__((packed));

// tcp header l4
struct tcphdr {
  uint16 sport; // source port
  uint16 dport; // dest port
  uint32 seq;   // seq number
  uint32 ack;   // ack number
  uint8 off;    // data offset
  uint8 flags;  // control flags
  uint16 win;   // receive window
  uint16 cksum; // checksum
  uint16 urp;   // urgent pointer
} __attribute__((packed));

// cac co tcp
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

// tuy chon tcp options (RFC 793, RFC 7323)
#define TCPOPT_EOL      0
#define TCPOPT_NOP      1
#define TCPOPT_MSS      2
#define TCPOPT_WSCALE   3
#define TCP_RCV_WSCALE  2  // factor shift 2 (x4 window)

struct tcp_options {
  uint16 mss;
  uint8  wscale;
  uint8  has_mss;
  uint8  has_wscale;
};

#define MBUF_SIZE         2048
#define MBUF_DEFAULT_HEAD 128

// tham so rto / circuit breaker
#define RTO_MIN      200   // ms
#define RTO_MAX      2000  // ms
#define MAX_BACKOFF  12    // so lan truyen lai lien tiep toi da
#ifndef ECONNRESET
#define ECONNRESET   104
#endif

// tham so kiem soat tac nghen (RFC 5681 - TCP Reno, Google BBR)
#define TCP_MSS      1400  // mss mac dinh trong xv6
#define CC_RENO      0
#define CC_BBR       1

// cac pha trong may trang thai Google BBR
#define BBR_STARTUP   0
#define BBR_DRAIN     1
#define BBR_PROBE_BW  2
#define BBR_PROBE_RTT 3

// bo dem goi tin mbuf
struct mbuf {
  struct mbuf *next;           // moc noi vao queue
  char        *head;           // con tro tro vao dau goi
  unsigned int len;            // do dai data
  int          ref_cnt;        // dem tham chieu
  uint32       seq;            // metadata: sequence bat dau cua segment
  uint32       seq_end;        // metadata: sequence ket thuc cua segment
  int          retransmitted;  // karn: 1 neu segment da bi gui lai (khong dung de do rtt)
  uint32       send_tick;      // tick luc gui lan dau, dung do rtt
  // tuan 5: dau thoi gian do phan giai us cho rtt va delivery rate (BBR)
  uint64       send_us;          // thoi diem gui lan dau (us)
  uint64       tx_delivered;     // pcb->delivered luc gui segment nay
  uint64       tx_delivered_us;  // pcb->delivered_us luc gui segment nay
  uint64       tx_first_sent_us; // pcb->first_sent_us luc gui segment nay
  char         buf[MBUF_SIZE]; // vung dem chua du lieu
};

// api mbuf (kernel/mbuf.c)
struct mbuf* mbufalloc(unsigned int headroom);
struct mbuf* mbufalloc_wait(unsigned int headroom);
void         mbuffree(struct mbuf *m);
char*        mbufpush(struct mbuf *m, unsigned int len);
char*        mbufpull(struct mbuf *m, unsigned int len);
char*        mbufput(struct mbuf *m, unsigned int len);
void         mbuftrim(struct mbuf *m, unsigned int len);
void         mbufinit(void);
int          mbuf_free_count(void);

// kiem toan bo nho mang - tuan 6 (TV1)
struct net_audit;
void         mbuf_audit(struct net_audit *a);
void         e1000_audit(struct net_audit *a);
void         arp_audit(struct net_audit *a);
void         arp_flush(void);
void         tcp_audit(struct net_audit *a);
void         net_audit_collect(struct net_audit *a, int flags);

// driver e1000 pci nic (kernel/e1000.c)
void         pci_init(void);
void         e1000_init(uint32 *regs);
int          e1000_transmit(struct mbuf *m);
void         e1000_recv(void);
void         e1000_intr(void);
int          e1000_set_loss_rate(int rate);
int          e1000_get_loss_rate(void);

// ethernet l2 (kernel/eth.c)
void eth_init(uint8 *mac_addr);
void eth_input(struct mbuf *m);
int eth_output(struct mbuf *m, uint8 *dst_mac, uint16 eth_type);

// arp (kernel/arp.c)
void arp_init(void);
void arp_age_tick(void);
void arp_input(struct mbuf *m);
int arp_resolve(uint32 ip, uint8 *mac_out, struct mbuf *pending_m);

// ipv4 l3 (kernel/ip.c)
void ip_init(uint32 host_ip);
void ip_input(struct mbuf *m);
int ip_output(struct mbuf *m, uint32 dst_ip, uint8 proto);

// checksum rfc 1071 (kernel/checksum.c)
uint16 in_cksum(const void *buf, int len);
uint16 tcp_checksum(struct mbuf *m, uint32 src_ip, uint32 dst_ip);

// cac trang thai trong tcp fsm
typedef enum {
  TCP_CLOSED = 0,
  TCP_LISTEN,
  TCP_SYN_SENT,
  TCP_SYN_RCVD,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT_1,
  TCP_FIN_WAIT_2,
  TCP_CLOSE_WAIT,
  TCP_CLOSING,
  TCP_LAST_ACK,
  TCP_TIME_WAIT
} tcp_state_t;

// tcp pcb quan ly ket noi
struct tcp_pcb {
  struct tcp_pcb *next;
  struct spinlock lock;

  // 4-tuple
  uint32 local_ip;
  uint16 local_port;
  uint32 remote_ip;
  uint16 remote_port;

  tcp_state_t state;

  // seq va window control
  uint32 snd_una; // unacknowledged seq
  uint32 snd_nxt; // next seq de gui
  uint32 snd_wnd; // send window
  uint32 iss;     // initial send seq

  uint32 rcv_nxt; // next seq cho nhan
  uint32 rcv_wnd; // receive window
  uint32 irs;     // initial receive seq

  // retransmit queue va timer tinh theo jacobson
  struct mbuf *retransmit_q;
  struct mbuf *ooo_queue; // hang doi goi den lech thu tu (out-of-order)
  uint32 srtt;
  uint32 rttvar;
  uint32 rto;
  int backoff;
  int conn_err;   // loi ket noi (ECONNRESET) khi circuit breaker kich hoat
  uint32 rto_deadline;

  // kiem soat tac nghen (RFC 5681 - TCP Reno & Google BBR - tuan 5 TV2)
  int    cc_algo;             // CC_RENO (0) hoac CC_BBR (1)
  uint32 cwnd;                // Congestion window (bytes)
  uint32 ssthresh;            // Slow start threshold (bytes)
  uint32 dupack_cnt;          // Dem duplicate ACKs
  uint32 recover;             // Sequence number danh dau ket thuc Fast Recovery

  // may trang thai Google BBR (tuan 5 TV2)
  int    bbr_state;           // BBR_STARTUP, BBR_DRAIN, BBR_PROBE_BW, BBR_PROBE_RTT
  uint32 bbr_max_bw;          // uoc luong bang thong cuc dai (bytes/s)
  uint32 bbr_bdp;             // Bandwidth-Delay Product = max_bw * min_rtt (bytes)
  uint32 bbr_target_cwnd;     // target cwnd = gain * BDP
  int    bbr_cycle_idx;       // pha chu ky 0..7 trong PROBE_BW
  uint64 bbr_cycle_stamp;     // thoi diem bat dau pha chu ky hien tai (us)
  uint64 bbr_rtt_stamp;       // thoi diem bat dau PROBE_RTT (us)
  uint64 bbr_round_delivered; // pcb->delivered tai moc round hien tai
  uint32 bbr_round_count;     // so round da hoan thanh
  uint32 bbr_bw_plateau_cnt;  // dem so round max_bw khong tang > 25% (STARTUP)
  uint32 bbr_prev_max_bw;     // max_bw tai round truoc

  // do thoi gian do phan giai us (tuan 5 - TV1, ha tang cho Google BBR)
  uint32 latest_rtt_us;     // mau rtt gan nhat (us, theo quy tac karn)
  uint32 min_rtt_us;        // rtt nho nhat trong cua so truot 10 giay
  uint64 min_rtt_stamp_us;  // thoi diem ghi nhan min_rtt_us
  int    min_rtt_expired;   // 1 neu mau gan nhat lam min_rtt het han 10s (-> BBR PROBE_RTT)
  uint32 rtt_samples;       // so mau rtt hop le
  uint64 delivered;         // tong so byte da duoc ack (tich luy)
  uint64 delivered_us;      // thoi diem delivered thay doi gan nhat
  uint64 first_sent_us;     // thoi diem gui cua segment moi nhat duoc ack
  uint32 delivery_rate;     // mau toc do chuyen phat gan nhat (bytes/s)
  uint32 rate_interval_us;  // do dai khoang do cua mau delivery_rate
  uint32 rate_delivered;    // so byte chuyen phat trong khoang do do

  // tuy chon window scale (RFC 7323)
  uint8 snd_wscale;          // scale factor tu peer
  uint8 rcv_wscale;          // scale factor cua xv6
  uint8 wscale_ok;           // 1 neu ca 2 ben dong y dung window scale
  struct tcp_pcb *syn_next;  // moc noi vao syn_q cua listen socket

  void *sock; // tro sang socket tuong ung
};

// thong ke tcp pcb phuc vu netstat va giam sat
#ifndef _STRUCT_TCP_STAT_
#define _STRUCT_TCP_STAT_
struct tcp_stat {
  uint32 local_ip;
  uint16 local_port;
  uint32 remote_ip;
  uint16 remote_port;
  int    state;
  uint32 cwnd;
  uint32 ssthresh;
  uint32 rtt;
  uint32 rto;
  uint32 snd_una;
  uint32 snd_nxt;
  uint32 snd_wnd;
  uint32 rcv_wnd;
  uint32 dupack_cnt;
  int    backoff;
  // tuan 5: thong so thoi gian do phan giai us
  uint32 rtt_us;         // mau rtt gan nhat (us)
  uint32 min_rtt_us;     // min rtt cua so truot 10s (us)
  uint32 delivery_rate;  // bytes/s
  // tuan 5: thong so BBR & Congestion Control (TV2)
  int    cc_algo;        // 0: Reno, 1: BBR
  int    bbr_state;      // 0: STARTUP, 1: DRAIN, 2: PROBE_BW, 3: PROBE_RTT
  uint32 max_bw;         // BBR max bandwidth (bytes/s)
  uint32 bdp;            // BBR BDP (bytes)
};
#endif

// tuan 6 (TV1): bien ban kiem toan bo nho mang (zero memory leak).
// moi mbuf trong pool phai nam o DUNG MOT trong cac noi sau:
//   free_list | rx ring e1000 | tx ring e1000 | arp pending_queue |
//   tcp retransmit_q | tcp ooo_queue
// leaked = mbuf_total - tong cac noi tren. o trang thai nghi phai = 0.
#ifndef _STRUCT_NET_AUDIT_
#define _STRUCT_NET_AUDIT_
struct net_audit {
  // pool mbuf (kernel/mbuf.c)
  int  mbuf_total;        // NMBUF
  int  mbuf_free;         // bien dem mbuf_free_count
  int  mbuf_free_walk;    // so phan tu dem duoc khi duyet free_list
  int  mbuf_refcnt_used;  // so mbuf co ref_cnt > 0 khi quet mang mbufs[]
  int  mbuf_bad;          // mbuf trong free_list co ref_cnt != 0 / ref_cnt < 0
  int  mbuf_min_free;     // muc ranh thap nhat tu luc boot
  uint mbuf_alloc_fail;
  uint mbuf_wait_sleeps;
  uint mbuf_double_free;
  // driver e1000 (kernel/e1000.c)
  int  rx_ring_held;      // mbuf gan thuong truc tren rx ring
  int  tx_inflight;       // mbuf con nam tren tx ring sau khi reclaim
  uint tx_packets;
  uint tx_loss_sim;       // goi bi bo gia lap mat goi drop
  uint tx_ring_full;
  uint rx_packets;
  uint rx_nombuf;
  int  loss_rate;         // ty le mat goi hien tai (%)
  // arp (kernel/arp.c)
  int  arp_valid;         // so entry da co mac
  int  arp_pending_entries;
  int  arp_pending_mbufs; // mbuf dang cho arp reply
  // tcp (kernel/tcp.c)
  int  tcp_pcb_total;     // kich thuoc pool pcb
  int  tcp_pcb_free;      // pcb nam trong free_list
  int  tcp_listen;
  int  tcp_conns;         // pcb khong CLOSED/LISTEN (dang ket noi)
  int  tcp_time_wait;
  int  tcp_zombie;        // pcb CLOSED, khong thuoc socket nao, khong o free_list
  int  tcp_rtx_mbufs;     // mbuf trong retransmit_q
  int  tcp_ooo_mbufs;     // mbuf trong ooo_queue
  // tong hop
  int  accounted;         // free + rx + tx + arp + rtx + ooo
  int  leaked;            // mbuf_total - accounted
};
#endif

#define NETAUDIT_FLUSH_ARP 0x1 // xoa sach bang arp truoc khi kiem toan

#define RINGBUF_SIZE 4096

// ring buffer cho socket
struct ringbuf {
  struct spinlock lock;
  char buf[RINGBUF_SIZE];
  int head;
  int tail;
  int count;
  int eof;
};

// struct socket gan vao struct file cua vfs
struct socket {
  struct spinlock lock;
  int type;
  int state;
  struct tcp_pcb *pcb;

  struct ringbuf rx_buf;
  struct ringbuf tx_buf;

  // hang doi listen backlog: Accept Queue (FIFO) va SYN Queue
  struct socket *backlog_q;
  struct socket *backlog_tail;
  struct socket *next;
  int backlog_max;
  int backlog_cnt;

  struct tcp_pcb *syn_q;     // hang doi cac ket noi dang bat tay (SYN_RCVD)
  int syn_cnt;               // so luong ket noi dang trong syn_q
  int syn_max;               // dung luong toi da cua syn_q

  int ref_cnt;
};

// api tcp (kernel/tcp.c, tcp_input.c, tcp_output.c, tcp_timer.c)
void tcp_init(void);
struct tcp_pcb *tcp_pcb_alloc(void);
void tcp_pcb_free(struct tcp_pcb *pcb);
void tcp_pcb_insert(struct tcp_pcb *pcb);
void tcp_pcb_set_listen(struct tcp_pcb *pcb);
struct tcp_pcb *tcp_lookup(uint32 src_ip, uint16 src_port, uint32 dst_ip,
                           uint16 dst_port);
void tcp_update_rto(struct tcp_pcb *pcb, uint32 rtt_msec);
void tcp_recalc_rto(struct tcp_pcb *pcb);
int  tcp_get_stats(struct tcp_stat *stats, int max_entries);
void tcp_dump_stats(void);

void tcp_input(struct mbuf *m, uint32 src_ip, uint32 dst_ip);
int tcp_output(struct tcp_pcb *pcb, struct mbuf *m, uint8 flags);
void tcp_send_rst(uint32 local_ip, uint16 local_port, uint32 remote_ip,
                  uint16 remote_port, uint32 in_seq, uint32 in_ack,
                  uint8 in_flags, int in_datalen);
void tcp_timer_tick(void);
void tcp_timer_process(void);

// do thoi gian us, min rtt 10s va delivery rate (kernel/tcp_timer.c, tuan 5)
#define TCP_MIN_RTT_WIN_US 10000000ULL // cua so truot min_rtt: 10 giay

// mau do lay tu segment moi nhat duoc ack (chua tung gui lai - quy tac karn)
struct tcp_rate_sample {
  uint64 send_us;          // thoi diem gui segment
  uint64 prior_delivered;  // pcb->delivered luc gui
  uint64 prior_us;         // pcb->delivered_us luc gui
  uint64 first_sent_us;    // pcb->first_sent_us luc gui
};

uint64 tcp_now_us(void);
void   tcp_rate_init(struct tcp_pcb *pcb);
void   tcp_rate_on_send(struct tcp_pcb *pcb, struct mbuf *seg);
void   tcp_rate_snapshot(struct mbuf *seg, struct tcp_rate_sample *rs);
int    tcp_rate_on_ack(struct tcp_pcb *pcb, uint32 acked, uint64 now_us,
                       struct tcp_rate_sample *rs);
int    tcp_update_min_rtt(struct tcp_pcb *pcb, uint32 rtt_us, uint64 now_us);
uint32 tcp_get_min_rtt_us(struct tcp_pcb *pcb);
int    tcp_min_rtt_expired(struct tcp_pcb *pcb, uint64 now_us);
void   tcp_timer_selftest(void);

// kiem soat tac nghen (kernel/tcp_cc.c)
void tcp_cc_init(struct tcp_pcb *pcb);
void tcp_cc_on_ack(struct tcp_pcb *pcb, uint32 ack);
void tcp_cc_on_dup_ack(struct tcp_pcb *pcb, uint32 ack);
void tcp_cc_on_timeout(struct tcp_pcb *pcb);
void tcp_fast_retransmit(struct tcp_pcb *pcb);
int  tcp_set_cc_algo(int algo);
int  tcp_get_cc_algo(void);
void tcp_pcb_update_cc(int algo);

// ringbuf api (kernel/ringbuf.c)
void ringbuf_init(struct ringbuf *rb);
int  ringbuf_write(struct ringbuf *rb, const char *data, int len);
int  ringbuf_read(struct ringbuf *rb, char *data, int len);
int  ringbuf_avail(struct ringbuf *rb);
int  ringbuf_free_space(struct ringbuf *rb);

// socket api (kernel/sock.c)
void sock_init(void);
struct socket *sock_alloc(void);
struct socket *sock_alloc_for_pcb(struct tcp_pcb *pcb);
void sock_close(struct socket *s);
void sock_push_eof(struct socket *s);
int sock_read(struct socket *s, uint64 addr, int n);
int sock_write(struct socket *s, uint64 addr, int n);
int sock_bind(struct socket *s, uint32 ip, uint16 port);
int sock_listen(struct socket *s, int backlog);
struct socket *sock_accept(struct socket *s);
int sock_connect(struct socket *s, uint32 ip, uint16 port);

#endif // _NET_H_
