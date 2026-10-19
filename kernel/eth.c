// xu ly ethernet frame l2
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

extern uint8 local_mac[ETH_ADDR_LEN];

void
eth_init(uint8 *mac)
{
  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    local_mac[i] = mac[i];
  }
}

// nhan frame tu e1000 len
void
eth_input(struct mbuf *m)
{
  struct eth *ethhdr;

  if (m->len < sizeof(struct eth)) {
    mbuffree(m);
    return;
  }

  ethhdr = (struct eth *)m->head;
  uint16 type = ntohs(ethhdr->type);

  // bo ethernet header roi day len l3
  mbufpull(m, sizeof(struct eth));

  if (type == ETH_TYPE_IP) {
    ip_input(m);
  } else if (type == ETH_TYPE_ARP) {
    arp_input(m);
  } else {
    // khong ho tro loai goi nay
    mbuffree(m);
  }
}

// dong goi ethernet frame va day xuong e1000
int
eth_output(struct mbuf *m, uint8 *dst_mac, uint16 eth_type)
{
  struct eth *ethhdr;

  ethhdr = (struct eth *)mbufpush(m, sizeof(struct eth));
  if (!ethhdr) {
    mbuffree(m);
    return -1;
  }

  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    ethhdr->shost[i] = local_mac[i];
    ethhdr->dhost[i] = dst_mac[i];
  }
  ethhdr->type = htons(eth_type);

  return e1000_transmit(m);
}
