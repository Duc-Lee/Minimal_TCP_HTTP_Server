// ============================================================================
// Thành viên 1 (TV1): Hạ tầng Mạng, Driver L2/L3 & Zero-Copy
// File: kernel/ip.c
// Nhiệm vụ: Xử lý tầng mạng IPv4 (L3)
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15); // Địa chỉ IP mặc định của QEMU guest

void
ip_init(uint32 host_ip)
{
  local_ip = host_ip;
}

// Xử lý gói tin IPv4 từ tầng Ethernet chuyển lên
void
ip_input(struct mbuf *m)
{
  struct ip *iphdr;

  if (m->len < sizeof(struct ip)) {
    mbuffree(m);
    return;
  }

  iphdr = (struct ip *)m->head;

  uint16 ck = in_cksum(iphdr, sizeof(struct ip));

  // Kiểm tra phiên bản IPv4
  if ((iphdr->vihl >> 4) != 4) {
    mbuffree(m);
    return;
  }

  // Kiểm tra Checksum IP header
  if (ck != 0) {
    mbuffree(m);
    return;
  }

  uint32 src_ip = ntohl(iphdr->src);
  uint32 dst_ip = ntohl(iphdr->dst);
  uint8 proto = iphdr->proto;

  // Cắt bỏ phần padding của Ethernet nếu có
  uint16 total_ip_len = ntohs(iphdr->len);
  if (m->len > total_ip_len) {
    mbuftrim(m, total_ip_len);
  }

  // Cắt bỏ IP Header để chuyển payload lên L4
  int hlen = (iphdr->vihl & 0x0f) * 4;
  mbufpull(m, hlen);

  if (proto == IP_PROTO_TCP) {
    // ------------------------------------------------------------------------
    // ĐIỂM GIAO TIẾP 1: TV1 -> TV2 (Khi có gói TCP từ card mạng lên)
    // TV1 gọi tcp_input(struct mbuf *m, uint32 src_ip, uint32 dst_ip)
    // ------------------------------------------------------------------------
    tcp_input(m, src_ip, dst_ip);
  } else {
    // Không hỗ trợ giao thức khác (ngoài TCP)
    mbuffree(m);
  }
}

// Đóng gói IPv4 header và gửi xuống tầng L2 Ethernet
int
ip_output(struct mbuf *m, uint32 dst_ip, uint8 proto)
{
  struct ip *iphdr;
  static uint16 ip_id = 1;

  iphdr = (struct ip *)mbufpush(m, sizeof(struct ip));
  if (!iphdr) {
    mbuffree(m);
    return -1;
  }

  iphdr->vihl = (4 << 4) | (sizeof(struct ip) / 4);
  iphdr->tos = 0;
  iphdr->len = htons(m->len);
  iphdr->id = htons(ip_id++);
  iphdr->off = 0;
  iphdr->ttl = 64;
  iphdr->proto = proto;
  iphdr->src = htonl(local_ip);
  iphdr->dst = htonl(dst_ip);
  iphdr->cksum = 0;
  iphdr->cksum = in_cksum(iphdr, sizeof(struct ip));

  // Phân giải MAC qua ARP rồi đẩy xuống Ethernet
  uint8 dst_mac[ETH_ADDR_LEN];
  int ares = arp_resolve(dst_ip, dst_mac, m);
  if (ares == 0) {
    return eth_output(m, dst_mac, ETH_TYPE_IP);
  }

  return 0; // Đang chờ ARP cache xử lý
}
