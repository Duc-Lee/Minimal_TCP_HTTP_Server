// driver e1000 pci nic cho xv6
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
#define RX_RING_SIZE 16

static volatile uint32 *regs;
struct spinlock e1000_lock;

// dma ring buffers, can align 16 byte
__attribute__((aligned(16))) static struct tx_desc tx_ring[TX_RING_SIZE];
static struct mbuf *tx_mbufs[TX_RING_SIZE];

__attribute__((aligned(16))) static struct rx_desc rx_ring[RX_RING_SIZE];
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// mac cua card e1000 trong qemu
uint8 local_mac[ETH_ADDR_LEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

// gia lap rot goi co the cau hinh qua syscall de test tcp retransmit
static uint32 packet_loss_seed = 12345;
static int packet_loss_rate = 10; // mac dinh 10%

int
e1000_set_loss_rate(int rate)
{
  if (rate < 0) rate = 0;
  if (rate > 100) rate = 100;
  acquire(&e1000_lock);
  packet_loss_rate = rate;
  release(&e1000_lock);
  return packet_loss_rate;
}

int
e1000_get_loss_rate(void)
{
  acquire(&e1000_lock);
  int r = packet_loss_rate;
  release(&e1000_lock);
  return r;
}

static int
should_drop_packet(void)
{
  if (packet_loss_rate <= 0)
    return 0;
  packet_loss_seed = (packet_loss_seed * 1103515245 + 12345) & 0x7fffffff;
  return (packet_loss_seed % 100) < packet_loss_rate;
}

// khoi tao dma ring tx va rx
void
e1000_init(uint32 *e1000_regs)
{
  int i;
  regs = e1000_regs;
  initlock(&e1000_lock, "e1000");

  acquire(&e1000_lock);

  (void)regs[E1000_STATUS / 4];

  // setup tx ring
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }

  uint64 tx_phys = (uint64)tx_ring;
  regs[E1000_TDBAL / 4] = tx_phys & 0xffffffff;
  regs[E1000_TDBAH / 4] = (tx_phys >> 32) & 0xffffffff;
  regs[E1000_TDLEN / 4] = TX_RING_SIZE * sizeof(struct tx_desc);
  regs[E1000_TDH / 4] = 0;
  regs[E1000_TDT / 4] = 0;

  // bat transmit: en | psp | ct=0x10 | cold=0x40
  regs[E1000_TCTL / 4] =
    E1000_TCTL_EN | E1000_TCTL_PSP | (0x10 << 4) | (0x40 << 12);
  // tipg: inter-packet gap theo ieee 802.3
  regs[E1000_TIPG / 4] = 10 | (8 << 10) | (6 << 20);

  // setup rx ring
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000_init: mbufalloc failed");
    rx_ring[i].addr = (uint64)rx_mbufs[i]->head;
    rx_ring[i].status = 0;
  }

  // nap dia chi mac vao thanh ghi ra[0]
  uint32 mac_lo = local_mac[0] | (local_mac[1] << 8) | (local_mac[2] << 16) |
                  (local_mac[3] << 24);
  uint32 mac_hi = local_mac[4] | (local_mac[5] << 8) | (1u << 31);
  regs[E1000_RA / 4] = mac_lo;
  regs[(E1000_RA + 4) / 4] = mac_hi;

  // clear bang multicast
  for (i = 0; i < 128; i++)
    regs[(E1000_MTA / 4) + i] = 0;

  uint64 rx_phys = (uint64)rx_ring;
  regs[E1000_RDBAL / 4] = rx_phys & 0xffffffff;
  regs[E1000_RDBAH / 4] = (rx_phys >> 32) & 0xffffffff;
  regs[E1000_RDLEN / 4] = RX_RING_SIZE * sizeof(struct rx_desc);
  regs[E1000_RDH / 4] = 0;
  regs[E1000_RDT / 4] = RX_RING_SIZE - 1;

  // bat receive: cho phep nhan broadcast
  regs[E1000_RCTL / 4] =
    E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC;

  // bat ngat rxt0
  regs[E1000_IMS / 4] = E1000_ICR_RXT0;

  regs[E1000_CTL / 4] |= E1000_CTL_SLU;

  release(&e1000_lock);
}

// truyen goi qua dma tx ring
int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000_lock);

  // khong drop goi arp de tranh mat phan giai mac
  uint16 eth_type = 0;
  if (m->len >= 14)
    eth_type = ntohs(*(uint16 *)(m->head + 12));

  if (eth_type != ETH_TYPE_ARP && should_drop_packet()) {
    release(&e1000_lock);
    mbuffree(m);
    return 0; // drop goi de test retransmit
  }

  uint32 tdt = regs[E1000_TDT / 4];
  struct tx_desc *d = &tx_ring[tdt % TX_RING_SIZE];

  if (!(d->status & E1000_TXD_STAT_DD)) {
    // tx ring day
    release(&e1000_lock);
    mbuffree(m);
    return -1;
  }

  if (tx_mbufs[tdt % TX_RING_SIZE]) {
    mbuffree(tx_mbufs[tdt % TX_RING_SIZE]);
    tx_mbufs[tdt % TX_RING_SIZE] = 0;
  }

  d->addr = (uint64)m->head;
  d->length = m->len;
  d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
  d->status = 0;
  tx_mbufs[tdt % TX_RING_SIZE] = m;

  regs[E1000_TDT / 4] = (tdt + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}

// quet rx ring khi co ngat nhan goi
void
e1000_recv(void)
{
  for (;;) {
    uint32 rdt = regs[E1000_RDT / 4];
    uint32 next = (rdt + 1) % RX_RING_SIZE;
    struct rx_desc *d = &rx_ring[next];

    if (!(d->status & E1000_RXD_STAT_DD))
      break;

    struct mbuf *m = rx_mbufs[next];
    m->len = d->length;

    // cap mbuf moi bu vao slot
    struct mbuf *newm = mbufalloc(0);
    if (newm) {
      rx_ring[next].addr = (uint64)newm->head;
      rx_ring[next].status = 0;
      rx_mbufs[next] = newm;
    } else {
      rx_ring[next].status = 0;
      regs[E1000_RDT / 4] = next;
      continue;
    }

    regs[E1000_RDT / 4] = next;

    // chuyen len eth_input, khong giu lock tranh deadlock
    eth_input(m);
  }
}

// xu ly ngat card e1000
void
e1000_intr(void)
{
  uint32 icr = regs[E1000_ICR / 4];

  if (icr & E1000_ICR_RXT0) {
    e1000_recv();
  }
}
