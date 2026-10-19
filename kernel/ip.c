// xu ly ip l3
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

uint32 local_ip = MAKE_IP_ADDR(10, 0, 2, 15); // ip mac dinh trong qemu

void
ip_init(uint32 host_ip)
{
  local_ip = host_ip;
}

// xu ly goi ipv4 tu ethernet len
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

  // kiem tra version 4
  if ((iphdr->vihl >> 4) != 4) {
    mbuffree(m);
    return;
  }

  // kiem tra checksum ip header
  if (ck != 0) {
    mbuffree(m);
    return;
  }

  uint32 src_ip = ntohl(iphdr->src);
  uint32 dst_ip = ntohl(iphdr->dst);
  uint8 proto = iphdr->proto;

  // cat padding ethernet neu co
  uint16 total_ip_len = ntohs(iphdr->len);
  if (m->len > total_ip_len) {
    mbuftrim(m, total_ip_len);
  }

  // bo ip header day len l4
  int hlen = (iphdr->vihl & 0x0f) * 4;
  mbufpull(m, hlen);

  if (proto == IP_PROTO_TCP) {
    // day payload len tcp_input
    tcp_input(m, src_ip, dst_ip);
  } else {
    // chi xu ly tcp
    mbuffree(m);
  }
}

// dong goi ipv4 header va day xuong ethernet
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

  // phan giai mac qua arp roi day xuong ethernet
  uint8 dst_mac[ETH_ADDR_LEN];
  int ares = arp_resolve(dst_ip, dst_mac, m);
  if (ares == 0) {
    return eth_output(m, dst_mac, ETH_TYPE_IP);
  }

  return 0; // dang cho arp tra ve mac
}
