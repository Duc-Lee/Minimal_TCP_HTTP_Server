// ============================================================================
// Thành viên 1 (TV1): Hạ tầng Mạng, Driver L2/L3 & Zero-Copy
// File: kernel/arp.c
// Nhiệm vụ: Phân giải ARP (giữ gói chờ MAC), bảng ARP Cache
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "net.h"

#define ARP_TABLE_SIZE 16

struct arp_entry {
  uint32 ip;
  uint8  mac[ETH_ADDR_LEN];
  int    valid;
  struct mbuf *pending_queue; // Hàng đợi giữ các gói tin chờ giải MAC
};

static struct {
  struct spinlock lock;
  struct arp_entry entries[ARP_TABLE_SIZE];
} arp_cache;

extern uint8  local_mac[ETH_ADDR_LEN];
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

// ─── Gửi ARP Reply (phải giữ arp_cache.lock trước khi gọi) ─────────────────
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

  arphdr->hrd = htons(1);              // Ethernet
  arphdr->pro = htons(ETH_TYPE_IP);   // IPv4
  arphdr->hln = ETH_ADDR_LEN;
  arphdr->pln = 4;
  arphdr->op  = htons(ARP_OP_REPLY);

  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    arphdr->sha[i] = local_mac[i];    // sender = xv6
    arphdr->tha[i] = target_mac[i];   // target = người hỏi
  }
  arphdr->sip = htonl(local_ip);
  arphdr->tip = htonl(target_ip);

  // Gửi ra ngoài qua eth_output (ARP không cần IP layer)
  eth_output(m, target_mac, ETH_TYPE_ARP);
}

// ─── Gửi ARP Request broadcast (phải giữ arp_cache.lock trước khi gọi) ─────
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

  static uint8 broadcast_mac[ETH_ADDR_LEN] = {0xff,0xff,0xff,0xff,0xff,0xff};
  static uint8 zero_mac[ETH_ADDR_LEN]      = {0};

  arphdr->hrd = htons(1);
  arphdr->pro = htons(ETH_TYPE_IP);
  arphdr->hln = ETH_ADDR_LEN;
  arphdr->pln = 4;
  arphdr->op  = htons(ARP_OP_REQUEST);

  for (int i = 0; i < ETH_ADDR_LEN; i++) {
    arphdr->sha[i] = local_mac[i];
    arphdr->tha[i] = zero_mac[i];
  }
  arphdr->sip = htonl(local_ip);
  arphdr->tip = htonl(target_ip);

  eth_output(m, broadcast_mac, ETH_TYPE_ARP);
}

// ─── arp_input: Xử lý gói tin ARP nhận được ─────────────────────────────────
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
  uint16 op  = ntohs(arphdr->op);

  if (hrd != 1 || pro != ETH_TYPE_IP) {
    mbuffree(m);
    return;
  }

  uint32 sip   = ntohl(arphdr->sip);

  acquire(&arp_cache.lock);

  // Cập nhật hoặc thêm entry vào ARP Cache từ thông tin sender
  int updated = 0;
  int free_slot = -1;
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_cache.entries[i].valid && arp_cache.entries[i].ip == sip) {
      // Cập nhật MAC mới nhất
      for (int j = 0; j < ETH_ADDR_LEN; j++)
        arp_cache.entries[i].mac[j] = arphdr->sha[j];

      // Gửi các gói tin đang chờ trong pending_queue
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

  // Học ARP sender mới vào cache
  if (!updated && free_slot >= 0) {
    arp_cache.entries[free_slot].ip = sip;
    for (int j = 0; j < ETH_ADDR_LEN; j++)
      arp_cache.entries[free_slot].mac[j] = arphdr->sha[j];
    arp_cache.entries[free_slot].valid = 1;
    arp_cache.entries[free_slot].pending_queue = 0;
  }

  // Nếu là ARP Request gửi tới địa chỉ IP của ta: phản hồi ARP Reply
  if (op == ARP_OP_REQUEST && ntohl(arphdr->tip) == local_ip) {
    uint8 requester_mac[ETH_ADDR_LEN];
    for (int i = 0; i < ETH_ADDR_LEN; i++)
      requester_mac[i] = arphdr->sha[i];
    release(&arp_cache.lock);
    mbuffree(m);
    // Gửi ARP Reply trả MAC của xv6 cho người hỏi
    arp_send_reply(sip, requester_mac);
    return;
  }

  release(&arp_cache.lock);
  mbuffree(m);
}

// ─── arp_resolve: Tìm MAC theo IP, nếu chưa có thì enqueue và broadcast ─────
int
arp_resolve(uint32 ip, uint8 *mac_out, struct mbuf *pending_m)
{
  acquire(&arp_cache.lock);

  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_cache.entries[i].valid && arp_cache.entries[i].ip == ip) {
      for (int j = 0; j < ETH_ADDR_LEN; j++)
        mac_out[j] = arp_cache.entries[i].mac[j];
      release(&arp_cache.lock);
      return 0; // Đã tìm thấy MAC
    }
  }

  // Chưa có trong cache: đưa gói tin vào pending_queue và gửi ARP Request
  if (pending_m) {
    int slot = -1;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
      if (!arp_cache.entries[i].valid) {
        slot = i;
        break;
      }
    }
    if (slot >= 0) {
      arp_cache.entries[slot].ip    = ip;
      arp_cache.entries[slot].valid = 0; // Chưa có MAC, chờ Reply
      // Nối vào đầu pending_queue
      pending_m->next = arp_cache.entries[slot].pending_queue;
      arp_cache.entries[slot].pending_queue = pending_m;
    }
    release(&arp_cache.lock);
    // Phát broadcast ARP Request
    arp_send_request(ip);
    return -1;
  }

  release(&arp_cache.lock);
  return -1; // Đang chờ giải ARP
}
