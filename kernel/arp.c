// phan giai arp va cache mac
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

#define ARP_TABLE_SIZE 16

struct arp_entry {
  uint32 ip;
  uint8 mac[ETH_ADDR_LEN];
  int valid;
  struct mbuf *pending_queue; // giu goi cho phan giai mac
};

static struct {
  struct spinlock lock;
  struct arp_entry entries[ARP_TABLE_SIZE];
} arp_cache;

extern uint8 local_mac[ETH_ADDR_LEN];
extern uint32 local_ip;

void
arp_init(void)
{
  initlock(&arp_cache.lock, "arp_cache");
  acquire(&arp_cache.lock);
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    arp_cache.entries[i].valid = 0;
    arp_cache.entries[i].pending_queue = 0;
  }
  release(&arp_cache.lock);
}

// gui arp reply ve cho host
static void
arp_send_reply(uint32 target_ip, uint8 *target_mac)
{
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEAD);
  if (!m)
    return;

  struct arp *arphdr = (struct arp *)mbufput(m, sizeof(struct arp));
  if (!arphdr) {
    mbuffree(m);
    return;
  }

  arphdr->hrd = htons(1);           // Ethernet
  arphdr->pro = htons(ETH_TYPE_IP); // IPv4
  arphdr->hln = ETH_ADDR_LEN;
  arphdr->pln = 4;
  arphdr->op = htons(ARP_OP_REPLY);

  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    arphdr->sha[i] = local_mac[i];  // sender = xv6
    arphdr->tha[i] = target_mac[i]; // target mac cua client
  }
  arphdr->sip = htonl(local_ip);
  arphdr->tip = htonl(target_ip);

  // gui ra eth_output, arp khong can ip header
  eth_output(m, target_mac, ETH_TYPE_ARP);
}

// broadcast arp request
static void
arp_send_request(uint32 target_ip)
{
  struct mbuf *m = mbufalloc(MBUF_DEFAULT_HEAD);
  if (!m)
    return;

  struct arp *arphdr = (struct arp *)mbufput(m, sizeof(struct arp));
  if (!arphdr) {
    mbuffree(m);
    return;
  }

  static uint8 broadcast_mac[ETH_ADDR_LEN] = {0xff, 0xff, 0xff,
                                              0xff, 0xff, 0xff};
  static uint8 zero_mac[ETH_ADDR_LEN] = {0};

  arphdr->hrd = htons(1);
  arphdr->pro = htons(ETH_TYPE_IP);
  arphdr->hln = ETH_ADDR_LEN;
  arphdr->pln = 4;
  arphdr->op = htons(ARP_OP_REQUEST);

  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    arphdr->sha[i] = local_mac[i];
    arphdr->tha[i] = zero_mac[i];
  }
  arphdr->sip = htonl(local_ip);
  arphdr->tip = htonl(target_ip);

  eth_output(m, broadcast_mac, ETH_TYPE_ARP);
}

// xu ly goi arp den
void
arp_input(struct mbuf *m)
{
  struct arp *arphdr;

  if (m->len < sizeof(struct arp)) {
    mbuffree(m);
    return;
  }

  arphdr = (struct arp *)m->head;

  uint16 hrd = ntohs(arphdr->hrd);
  uint16 pro = ntohs(arphdr->pro);
  uint16 op = ntohs(arphdr->op);

  if (hrd != 1 || pro != ETH_TYPE_IP) {
    mbuffree(m);
    return;
  }

  uint32 sip = ntohl(arphdr->sip);

  acquire(&arp_cache.lock);

  // cap nhat hoac hoc entry moi tu sender
  int updated = 0;
  int free_slot = -1;
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_cache.entries[i].valid && arp_cache.entries[i].ip == sip) {
      for (int j = 0; j < ETH_ADDR_LEN; j++)
        arp_cache.entries[i].mac[j] = arphdr->sha[j];

      // day cac goi dang cho trong queue di tiep
      struct mbuf *p = arp_cache.entries[i].pending_queue;
      arp_cache.entries[i].pending_queue = 0;
      while (p) {
        struct mbuf *next = p->next;
        p->next = 0;
        eth_output(p, arp_cache.entries[i].mac, ETH_TYPE_IP);
        p = next;
      }
      updated = 1;
      break;
    }
    if (!arp_cache.entries[i].valid && free_slot < 0)
      free_slot = i;
  }

  // them vao slot trong
  if (!updated && free_slot >= 0) {
    arp_cache.entries[free_slot].ip = sip;
    for (int j = 0; j < ETH_ADDR_LEN; j++)
      arp_cache.entries[free_slot].mac[j] = arphdr->sha[j];
    arp_cache.entries[free_slot].valid = 1;
    arp_cache.entries[free_slot].pending_queue = 0;
  }

  // neu hoi dung ip cua minh thi tra loi reply
  if (op == ARP_OP_REQUEST && ntohl(arphdr->tip) == local_ip) {
    uint8 requester_mac[ETH_ADDR_LEN];
    for (int i = 0; i < ETH_ADDR_LEN; i++)
      requester_mac[i] = arphdr->sha[i];
    release(&arp_cache.lock);
    mbuffree(m);
    arp_send_reply(sip, requester_mac);
    return;
  }

  release(&arp_cache.lock);
  mbuffree(m);
}

// tim mac theo ip, neu chua co thi luu vao queue va broadcast request
int
arp_resolve(uint32 ip, uint8 *mac_out, struct mbuf *pending_m)
{
  acquire(&arp_cache.lock);

  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_cache.entries[i].valid && arp_cache.entries[i].ip == ip) {
      for (int j = 0; j < ETH_ADDR_LEN; j++)
        mac_out[j] = arp_cache.entries[i].mac[j];
      release(&arp_cache.lock);
      return 0; // da co mac trong cache
    }
  }

  // chua co trong cache: treo goi tin vao pending_queue va gui arp request
  if (pending_m) {
    int slot = -1;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
      if (!arp_cache.entries[i].valid) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      arp_cache.entries[slot].ip = ip;
      arp_cache.entries[slot].valid = 0; // chua co mac, cho reply
      pending_m->next = arp_cache.entries[slot].pending_queue;
      arp_cache.entries[slot].pending_queue = pending_m;
    }
    release(&arp_cache.lock);
    // broadcast arp request
    arp_send_request(ip);
    return -1;
  }

  release(&arp_cache.lock);
  return -1; // dang cho arp reply
}
