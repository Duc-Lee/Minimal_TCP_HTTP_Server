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

#define MBUF_SIZE         2048
#define MBUF_DEFAULT_HEAD 128

// bo dem goi tin mbuf
struct mbuf {
  struct mbuf *next;           // moc noi vao queue
  char        *head;           // con tro tro vao dau goi
  unsigned int len;            // do dai data
  int          ref_cnt;        // dem tham chieu
  uint32       seq_end;        // metadata: sequence ket thuc cua segment
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
  uint32 srtt;
  uint32 rttvar;
  uint32 rto;
  int backoff;
  uint32 rto_deadline;
  uint32 dup_acks; // dem duplicate ack de fast retransmit

  void *sock; // tro sang socket tuong ung
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

void tcp_input(struct mbuf *m, uint32 src_ip, uint32 dst_ip);
int tcp_output(struct tcp_pcb *pcb, struct mbuf *m, uint8 flags);
void tcp_timer_tick(void);
void tcp_timer_process(void);

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

void ringbuf_init(struct ringbuf *rb);
int ringbuf_write(struct ringbuf *rb, const char *data, int len);
int ringbuf_read(struct ringbuf *rb, char *data, int len);
int ringbuf_avail(struct ringbuf *rb);
int ringbuf_free_space(struct ringbuf *rb);

// struct socket gan vao struct file cua vfs
struct socket {
  struct spinlock lock;
  int type;
  int state;
  struct tcp_pcb *pcb;

  struct ringbuf rx_buf;
  struct ringbuf tx_buf;

  // hang doi listen backlog
  struct socket *backlog_q;
  struct socket *next;
  int backlog_max;
  int backlog_cnt;

  int ref_cnt;
};

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
