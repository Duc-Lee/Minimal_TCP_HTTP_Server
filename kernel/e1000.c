// driver e1000 pci nic cho xv6
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "e1000_dev.h"
#include "net.h"

// tuan 4: nang ring tu 16 len 32 descriptor de chiu burst tu nhieu worker.
// tdlen/rdlen phai la boi so 128 byte (8 descriptor x 16 byte).
// rx ring giu thuong truc RX_RING_SIZE mbuf cua pool (xem NMBUF trong mbuf.c).
#define TX_RING_SIZE 32
#define RX_RING_SIZE 32

// so lan poll lai khi tx ring day truoc khi drop goi
#define TX_FULL_SPIN 64

static volatile uint32 *regs;
struct spinlock e1000_lock;

// dma ring buffers, can align 16 byte
__attribute__((aligned(16))) static struct tx_desc tx_ring[TX_RING_SIZE];
static struct mbuf *tx_mbufs[TX_RING_SIZE];

__attribute__((aligned(16))) static struct rx_desc rx_ring[RX_RING_SIZE];
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// quan ly tx ring bang chi so phan mem (bao ve boi e1000_lock):
// cac slot [tx_clean, tx_clean + tx_inflight) dang cho card gui / cho thu hoi.
// card coi ring rong khi tdh == tdt nen toi da TX_RING_SIZE - 1 slot in-flight.
static uint32 tx_tail;  // slot ke tiep driver se ghi (ban sao cua tdt)
static uint32 tx_clean; // slot cu nhat chua thu hoi
static int tx_inflight; // so slot dang giu mbuf

// thong ke driver. tx_* bao ve boi e1000_lock; rx_* chi cap nhat trong
// e1000_intr (plic chi giao irq e1000 cho 1 hart tai 1 thoi diem).
static struct {
  uint tx_packets;     // goi da dua xuong card
  uint tx_reclaimed;   // descriptor tx da thu hoi mbuf
  uint tx_ring_full;   // goi bi drop vi tx ring day
  uint tx_loss_sim;    // goi bi drop boi bo gia lap mat goi (setloss)
  int tx_inflight_max; // so slot tx in-flight cao nhat tung thay
  uint rx_packets;     // goi nhan chuyen len eth_input
  uint rx_nombuf;      // goi nhan bi drop vi het mbuf de bu vao ring
  uint rx_overrun;     // so lan card bao rx overrun (ring het descriptor)
} e1000_stats;

static int
is_pow2(uint n)
{
  return n != 0 && (n & (n - 1)) == 0;
}

// doc status descriptor qua volatile vi card ghi vao bang dma
static inline uint8
desc_status(volatile uint8 *status)
{
  return *status;
}

// mac cua card e1000 trong qemu
uint8 local_mac[ETH_ADDR_LEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

// gia lap rot goi co the cau hinh qua syscall de test tcp retransmit
static uint32 packet_loss_seed = 12345;
static int packet_loss_rate = 0; // mac dinh 0%, bat bang setloss khi can test

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
  tx_tail = 0;
  tx_clean = 0;
  tx_inflight = 0;

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

  // bat ngat:
  //  rxt0   : co goi moi
  //  rxdmt0 : so descriptor rx con trong xuong duoi nguong (1/2 ring) -> don som
  //  rxo    : rx overrun, card het descriptor -> dem va don ngay
  //  txqe   : tx queue rong -> thu hoi mbuf cua cac goi da gui xong
  regs[E1000_IMS / 4] =
    E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO | E1000_ICR_TXQE;

  regs[E1000_CTL / 4] |= E1000_CTL_SLU;

  release(&e1000_lock);

  printk("e1000: rx_ring=%d tx_ring=%d\n", RX_RING_SIZE, TX_RING_SIZE);
}

// thu hoi cac descriptor tx card da gui xong (dd=1) va tra mbuf ve pool ngay,
// thay vi doi den khi slot duoc dung lai (truoc day co the giam toi 16 mbuf).
// goi khi dang giu e1000_lock. tra ve so slot da thu hoi.
static int
e1000_tx_reclaim_locked(void)
{
  int n = 0;

  while (tx_inflight > 0) {
    if (!(desc_status(&tx_ring[tx_clean].status) & E1000_TXD_STAT_DD))
      break;
    if (tx_mbufs[tx_clean]) {
      mbuffree(tx_mbufs[tx_clean]);
      tx_mbufs[tx_clean] = 0;
    }
    tx_clean = (tx_clean + 1) % TX_RING_SIZE;
    tx_inflight--;
    n++;
  }
  e1000_stats.tx_reclaimed += n;
  return n;
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
    e1000_stats.tx_loss_sim++;
    release(&e1000_lock);
    mbuffree(m);
    return 0; // drop goi de test retransmit
  }

  // don cac slot da gui xong truoc, tranh ring day gia
  e1000_tx_reclaim_locked();

  // ring van day: card (qemu) gui rat nhanh, poll them vai lan cho dd duoc bat
  for (int spin = 0; tx_inflight >= TX_RING_SIZE - 1 && spin < TX_FULL_SPIN;
       spin++)
    e1000_tx_reclaim_locked();

  if (tx_inflight >= TX_RING_SIZE - 1) {
    // tx ring day that su: drop, tcp se truyen lai sau rto
    uint nfull = ++e1000_stats.tx_ring_full;
    release(&e1000_lock);
    mbuffree(m);
    if (is_pow2(nfull))
      printk("[e1000] WARN tx ring full, drop (total=%d)\n", nfull);
    return -1;
  }

  struct tx_desc *d = &tx_ring[tx_tail];
  if (tx_mbufs[tx_tail]) {
    // khong the xay ra neu tx_inflight dung, phong thu de khong ro ri mbuf
    mbuffree(tx_mbufs[tx_tail]);
    tx_mbufs[tx_tail] = 0;
  }

  d->addr = (uint64)m->head;
  d->length = m->len;
  d->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
  d->status = 0;
  tx_mbufs[tx_tail] = m;

  tx_tail = (tx_tail + 1) % TX_RING_SIZE;
  tx_inflight++;
  if (tx_inflight > e1000_stats.tx_inflight_max)
    e1000_stats.tx_inflight_max = tx_inflight;
  e1000_stats.tx_packets++;

  // descriptor phai ghi xong vao ram truoc khi bao card qua tdt
  __sync_synchronize();
  regs[E1000_TDT / 4] = tx_tail;

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

    if (!(desc_status(&d->status) & E1000_RXD_STAT_DD))
      break;

    struct mbuf *m = rx_mbufs[next];
    m->len = d->length;

    // cap mbuf moi bu vao slot
    struct mbuf *newm = mbufalloc(0);
    if (!newm) {
      // pool can: thu hoi mbuf dang nam o tx ring roi thu lai mot lan
      acquire(&e1000_lock);
      int got = e1000_tx_reclaim_locked();
      release(&e1000_lock);
      if (got > 0)
        newm = mbufalloc(0);
    }

    if (newm) {
      rx_ring[next].addr = (uint64)newm->head;
      rx_ring[next].status = 0;
      rx_mbufs[next] = newm;
    } else {
      // van het mbuf: bo goi nay, giu buffer cu trong ring de card con cho ghi
      uint ndrop = ++e1000_stats.rx_nombuf;
      rx_ring[next].status = 0;
      __sync_synchronize();
      regs[E1000_RDT / 4] = next;
      if (is_pow2(ndrop))
        printk("[e1000] WARN rx drop: no mbuf (total=%d)\n", ndrop);
      continue;
    }

    // tra slot cho card ngay de card co cho ghi goi tiep theo
    __sync_synchronize();
    regs[E1000_RDT / 4] = next;
    e1000_stats.rx_packets++;

    // chuyen len eth_input, khong giu lock tranh deadlock
    eth_input(m);
  }
}

// xu ly ngat card e1000
void
e1000_intr(void)
{
  // doc icr se xoa cac bit nguyen nhan; doc lai vai vong de khong bo sot su kien
  // den trong luc dang xu ly (vd. goi moi den khi dang chay eth_input)
  for (int round = 0; round < 4; round++) {
    uint32 icr = regs[E1000_ICR / 4];
    if (icr == 0)
      break;

    if (icr & E1000_ICR_RXO) {
      uint nover = ++e1000_stats.rx_overrun;
      if (is_pow2(nover))
        printk("[e1000] WARN rx overrun (total=%d)\n", nover);
    }

    if (icr & (E1000_ICR_TXQE | E1000_ICR_TXDW)) {
      acquire(&e1000_lock);
      e1000_tx_reclaim_locked();
      release(&e1000_lock);
    }

    if (icr & (E1000_ICR_RXT0 | E1000_ICR_RXDMT0 | E1000_ICR_RXO))
      e1000_recv();
  }
}
