#ifndef _NET_H_
#define _NET_H_

#include "types.h"
#include "spinlock.h"

// ============================================================================
// GIAI ĐOẠN 1: CỐT LÕI (BẮT BUỘC QUA MÔN)
// TCP/IP Stack: Ethernet, ARP, IPv4, TCP FSM 11 trạng thái, Sliding Window,
// RTO Timer, E1000 DMA Ring + bộ lọc rớt gói 10%, Socket API & Web Server (HTTP/1.0)
// ============================================================================

// --- Network Byte Order Utility Macros ---
static inline uint16 bswap16(uint16 x) {
  return ((x & 0x00ff) << 8) | ((x & 0xff00) >> 8);
}

static inline uint32 bswap32(uint32 x) {
  return ((x & 0x000000ff) << 24) |
         ((x & 0x0000ff00) << 8)  |
         ((x & 0x00ff0000) >> 8)  |
         ((x & 0xff000000) >> 24);
}

#define htons(x) bswap16(x)
#define ntohs(x) bswap16(x)
#define htonl(x) bswap32(x)
#define ntohl(x) bswap32(x)

#define MAKE_IP_ADDR(a, b, c, d) \
  (((uint32)(a) << 24) | ((uint32)(b) << 16) | ((uint32)(c) << 8) | (uint32)(d))

// --- Protocol Constants ---
#define ETH_ADDR_LEN      6
#define ETH_TYPE_IP       0x0800
#define ETH_TYPE_ARP      0x0806

#define IP_PROTO_ICMP     1
#define IP_PROTO_TCP      6

#define ARP_OP_REQUEST    1
#define ARP_OP_REPLY      2

// --- L2: Ethernet Header ---
struct eth {
  uint8  dhost[ETH_ADDR_LEN];
  uint8  shost[ETH_ADDR_LEN];
  uint16 type;
} __attribute__((packed));

// --- ARP Packet Format ---
struct arp {
  uint16 hrd;                  // Hardware address format (1 = Ethernet)
  uint16 pro;                  // Protocol address format (0x0800 = IPv4)
  uint8  hln;                  // Hardware address length (6)
  uint8  pln;                  // Protocol address length (4)
  uint16 op;                   // Opcode (1=request, 2=reply)
  uint8  sha[ETH_ADDR_LEN];    // Sender hardware address
  uint32 sip;                  // Sender IP address
  uint8  tha[ETH_ADDR_LEN];    // Target hardware address
  uint32 tip;                  // Target IP address
} __attribute__((packed));

// --- L3: IPv4 Header ---
struct ip {
  uint8  vihl;                 // Version (4 bits) + IHL (4 bits)
  uint8  tos;                  // Type of Service
  uint16 len;                  // Total Length
  uint16 id;                   // Identification
  uint16 off;                  // Flags & Fragment Offset
  uint8  ttl;                  // Time to Live
  uint8  proto;                // Protocol (IP_PROTO_TCP)
  uint16 cksum;                // Header Checksum
  uint32 src;                  // Source IP address
  uint32 dst;                  // Destination IP address
} __attribute__((packed));

// --- L4: TCP Header ---
struct tcphdr {
  uint16 sport;                // Source Port
  uint16 dport;                // Destination Port
  uint32 seq;                  // Sequence Number
  uint32 ack;                  // Acknowledgment Number
  uint8  off;                  // Data Offset
  uint8  flags;                // Control Flags
  uint16 win;                  // Receive Window Size
  uint16 cksum;                // Checksum
  uint16 urp;                  // Urgent Pointer
} __attribute__((packed));

// TCP Control Flags
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

// ============================================================================
// TV1: THÀNH VIÊN 1 - HẠ TẦNG MẠNG, DRIVER L2/L3 & BỘ ĐỆM MBUF
// ============================================================================

#define MBUF_SIZE          2048
#define MBUF_DEFAULT_HEAD  128

// Bộ đệm mbuf quản lý gói tin
struct mbuf {
  struct mbuf *next;           // Kế tiếp trong hàng đợi
  char        *head;           // Con trỏ tới đầu dữ liệu
  unsigned int len;            // Chiều dài dữ liệu
  int          ref_cnt;        // Biến đếm tham chiếu (Reference counter)
  char         buf[MBUF_SIZE]; // Vùng đệm vật lý
};

// mbuf API (kernel/mbuf.c)
struct mbuf* mbufalloc(unsigned int headroom);
void         mbuffree(struct mbuf *m);
char*        mbufpush(struct mbuf *m, unsigned int len);
char*        mbufpull(struct mbuf *m, unsigned int len);
char*        mbufput(struct mbuf *m, unsigned int len);
void         mbuftrim(struct mbuf *m, unsigned int len);
void         mbufinit(void);

// E1000 Driver & Bộ lọc giả lập rớt gói 10% (kernel/e1000.c)
void         pci_init(void);
void         e1000_init(uint32 *regs);
int          e1000_transmit(struct mbuf *m);
void         e1000_recv(void);
void         e1000_intr(void);

// L2 Ethernet (kernel/eth.c)
void         eth_init(uint8 *mac_addr);
void         eth_input(struct mbuf *m);
int          eth_output(struct mbuf *m, uint8 *dst_mac, uint16 eth_type);

// ARP (kernel/arp.c)
void         arp_init(void);
void         arp_input(struct mbuf *m);
int          arp_resolve(uint32 ip, uint8 *mac_out, struct mbuf *pending_m);

// L3 IPv4 (kernel/ip.c)
void         ip_init(uint32 host_ip);
void         ip_input(struct mbuf *m);
int          ip_output(struct mbuf *m, uint32 dst_ip, uint8 proto);

// Checksum RFC 1071 (kernel/checksum.c)
uint16       in_cksum(const void *buf, int len);
uint16       tcp_checksum(struct mbuf *m, uint32 src_ip, uint32 dst_ip);

// ============================================================================
// TV2: THÀNH VIÊN 2 - LÕI GIAO VẬN TCP, SLIDING WINDOW & RTO TIMER
// ============================================================================

// 11 Trạng thái TCP FSM
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

// TCP Protocol Control Block (PCB) cho Giai đoạn 1
struct tcp_pcb {
  struct tcp_pcb *next;
  struct spinlock lock;

  // 4-tuple nhận diện kết nối
  uint32 local_ip;
  uint16 local_port;
  uint32 remote_ip;
  uint16 remote_port;

  tcp_state_t state;           // 11 trạng thái FSM

  // Sequence Numbers & Cửa sổ trượt (Sliding Window)
  uint32 snd_una;              // Cạnh trái Send Window (oldest unacknowledged)
  uint32 snd_nxt;              // Sequence tiếp theo gửi đi
  uint32 snd_wnd;              // Kích thước Send Window
  uint32 iss;                  // Initial Send Sequence Number

  uint32 rcv_nxt;              // Sequence tiếp theo kỳ vọng nhận
  uint32 rcv_wnd;              // Kích thước Receive Window
  uint32 irs;                  // Initial Receive Sequence Number

  // Hàng đợi truyền lại & RTO Timer (Jacobson/Karels)
  struct mbuf *retransmit_q;   // Hàng đợi các segment chờ ACK
  uint32 srtt;                 // Smoothed Round Trip Time
  uint32 rttvar;               // Round Trip Time Variation
  uint32 rto;                  // Retransmission Timeout (ms)
  int    backoff;              // Exponential Backoff factor
  uint32 rto_deadline;         // Thời điểm (ticks) hết hạn RTO
  uint32 dup_acks;             // Đếm số Duplicate ACKs nhận liên tiếp (Fast Retransmit)

  // Trỏ tới Socket
  void  *sock;
};

// TCP API (kernel/tcp.c, tcp_input.c, tcp_output.c, tcp_timer.c)
void            tcp_init(void);
struct tcp_pcb* tcp_pcb_alloc(void);
void            tcp_pcb_free(struct tcp_pcb *pcb);
void            tcp_pcb_insert(struct tcp_pcb *pcb);   // Thêm vào hash table sau khi có 4-tuple
void            tcp_pcb_set_listen(struct tcp_pcb *pcb); // Đăng ký vào listen list
struct tcp_pcb* tcp_lookup(uint32 src_ip, uint16 src_port, uint32 dst_ip, uint16 dst_port);
void            tcp_update_rto(struct tcp_pcb *pcb, uint32 rtt_msec);

// TV1 -> TV2: Khi có gói tin TCP từ tầng IP lên
void            tcp_input(struct mbuf *m, uint32 src_ip, uint32 dst_ip);

// TV2 -> TV1: Truyền segment TCP xuống IP
int             tcp_output(struct tcp_pcb *pcb, struct mbuf *m, uint8 flags);

// TCP Timer cho RTO
void            tcp_timer_tick(void);
void            tcp_timer_process(void);

// ============================================================================
// TV3: THÀNH VIÊN 3 - TẦNG SOCKET VFS & WEB SERVER HTTP/1.0
// ============================================================================

#define RINGBUF_SIZE 4096

// Bộ đệm vòng Ring Buffer cho Socket TX/RX
struct ringbuf {
  struct spinlock lock;
  char  buf[RINGBUF_SIZE];
  int   head;                  // Con trỏ ghi
  int   tail;                  // Con trỏ đọc
  int   count;                 // Số byte hiện có
  int   eof;                   // Cờ báo kết thúc (EOF)
};

void ringbuf_init(struct ringbuf *rb);
int  ringbuf_write(struct ringbuf *rb, const char *data, int len);
int  ringbuf_read(struct ringbuf *rb, char *data, int len);
int  ringbuf_avail(struct ringbuf *rb);
int  ringbuf_free_space(struct ringbuf *rb);

// Cấu trúc Socket gắn vào VFS struct file
struct socket {
  struct spinlock lock;
  int    type;                 // SOCK_STREAM
  int    state;
  struct tcp_pcb *pcb;         // Trỏ tới lõi TCP PCB

  struct ringbuf rx_buf;       // Ring buffer nhận
  struct ringbuf tx_buf;       // Ring buffer gửi

  // Backlog cho listen/accept
  struct socket *backlog_q;
  struct socket *next;
  int    backlog_max;
  int    backlog_cnt;

  int    ref_cnt;              // Đếm tham chiếu
};

// Socket API (kernel/sock.c)
void           sock_init(void);
struct socket* sock_alloc(void);
struct socket* sock_alloc_for_pcb(struct tcp_pcb *pcb);
void           sock_close(struct socket *s);
void           sock_push_eof(struct socket *s);
int            sock_read(struct socket *s, uint64 addr, int n);
int            sock_write(struct socket *s, uint64 addr, int n);
int            sock_bind(struct socket *s, uint32 ip, uint16 port);
int            sock_listen(struct socket *s, int backlog);
struct socket* sock_accept(struct socket *s);
int            sock_connect(struct socket *s, uint32 ip, uint16 port);

#endif // _NET_H_
