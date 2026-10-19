// ============================================================================
// Thành viên 1 (TV1): Hạ tầng Mạng, Driver L2/L3 & Zero-Copy
// File: kernel/e1000.c
// Nhiệm vụ: Driver E1000 DMA Ring, NAPI Polling & bộ lọc giả lập rớt gói 10%
// ============================================================================

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

// DMA descriptor rings — phải 16-byte aligned
__attribute__((aligned(16))) static struct tx_desc tx_ring[TX_RING_SIZE];
static struct mbuf *tx_mbufs[TX_RING_SIZE];

__attribute__((aligned(16))) static struct rx_desc rx_ring[RX_RING_SIZE];
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// MAC address của card mạng E1000
uint8 local_mac[ETH_ADDR_LEN] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

// ─── Bộ lọc giả lập rớt gói 10% (Packet Loss Simulator) ─────────────────────
static uint32 packet_loss_seed = 12345;
static int
should_drop_packet_10pct(void)
{
  // LCG random generator đơn giản trong kernel
  packet_loss_seed = (packet_loss_seed * 1103515245 + 12345) & 0x7fffffff;
  return (packet_loss_seed % 100) < 10; // Rớt 10% gói tin
}

// ─── e1000_init: Khởi tạo DMA ring TX/RX ────────────────────────────────────
void
e1000_init(uint32 *e1000_regs)
{
  int i;
  regs = e1000_regs;
  initlock(&e1000_lock, "e1000");

  acquire(&e1000_lock);

  // [E1000 14.5] Không reset thiết bị vì QEMU e1000 reset không đúng chuẩn
  (void)regs[E1000_STATUS / 4];

  // ── TX Ring setup ──────────────────────────────────────────────────────────
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD; // Đánh dấu tất cả descriptor là "free"
    tx_mbufs[i] = 0;
  }

  uint64 tx_phys = (uint64)tx_ring;
  regs[E1000_TDBAL / 4] = tx_phys & 0xffffffff;
  regs[E1000_TDBAH / 4] = (tx_phys >> 32) & 0xffffffff;
  regs[E1000_TDLEN / 4] = TX_RING_SIZE * sizeof(struct tx_desc);
  regs[E1000_TDH / 4]   = 0;
  regs[E1000_TDT / 4]   = 0;

  // Bật transmit: EN | PSP | CT=0x10 | COLD=0x40
  regs[E1000_TCTL / 4] = E1000_TCTL_EN | E1000_TCTL_PSP |
                          (0x10 << 4) | (0x40 << 12);
  // TIPG: inter-packet gap theo IEEE 802.3 standard (TIPG = 10 | 8<<10 | 6<<20)
  regs[E1000_TIPG / 4] = 10 | (8 << 10) | (6 << 20);

  // ── RX Ring setup ──────────────────────────────────────────────────────────
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000_init: mbufalloc failed");
    rx_ring[i].addr   = (uint64)rx_mbufs[i]->head;
    rx_ring[i].status = 0;
  }

  // Ghi MAC address vào thanh ghi RA[0]
  uint32 mac_lo = local_mac[0] | (local_mac[1] << 8) |
                  (local_mac[2] << 16) | (local_mac[3] << 24);
  uint32 mac_hi = local_mac[4] | (local_mac[5] << 8) | (1u << 31); // AV bit
  regs[E1000_RA / 4]       = mac_lo;
  regs[(E1000_RA + 4) / 4] = mac_hi;

  // Xóa bảng Multicast
  for (i = 0; i < 128; i++)
    regs[(E1000_MTA / 4) + i] = 0;

  uint64 rx_phys = (uint64)rx_ring;
  regs[E1000_RDBAL / 4] = rx_phys & 0xffffffff;
  regs[E1000_RDBAH / 4] = (rx_phys >> 32) & 0xffffffff;
  regs[E1000_RDLEN / 4] = RX_RING_SIZE * sizeof(struct rx_desc);
  regs[E1000_RDH / 4]   = 0;
  regs[E1000_RDT / 4]   = RX_RING_SIZE - 1; // Tail = last descriptor

  // Bật nhận: EN | BAM (chấp nhận broadcast) | SZ_2048 | SECRC
  regs[E1000_RCTL / 4] = E1000_RCTL_EN | E1000_RCTL_BAM |
                          E1000_RCTL_SZ_2048 | E1000_RCTL_SECRC;

  // Bật ngắt RXT0 (receive timer interrupt)
  regs[E1000_IMS / 4] = E1000_ICR_RXT0;

  // Force link up
  regs[E1000_CTL / 4] |= E1000_CTL_SLU;

  release(&e1000_lock);
}

// ─── e1000_transmit: Gửi gói tin qua DMA TX Ring ────────────────────────────
int
e1000_transmit(struct mbuf *m)
{
  acquire(&e1000_lock);

  // Không giả lập rớt gói đối với gói ARP (L2 resolution quan trọng)
  uint16 eth_type = 0;
  if (m->len >= 14)
    eth_type = ntohs(*(uint16 *)(m->head + 12));

  if (eth_type != ETH_TYPE_ARP && should_drop_packet_10pct()) {
    release(&e1000_lock);
    mbuffree(m);
    return 0; // Giả lập gói tin bị thất thoát — TCP sẽ retransmit sau RTO
  }

  // Lấy vị trí tail hiện tại của TX ring
  uint32 tdt = regs[E1000_TDT / 4];
  struct tx_desc *d = &tx_ring[tdt % TX_RING_SIZE];

  // Kiểm tra descriptor đã được hardware giải phóng (cờ DD set)
  if (!(d->status & E1000_TXD_STAT_DD)) {
    // TX ring đầy — không thể gửi
    release(&e1000_lock);
    mbuffree(m);
    return -1;
  }

  // Giải phóng mbuf cũ nếu còn
  if (tx_mbufs[tdt % TX_RING_SIZE]) {
    mbuffree(tx_mbufs[tdt % TX_RING_SIZE]);
    tx_mbufs[tdt % TX_RING_SIZE] = 0;
  }

  // Ghi DMA address và độ dài vào descriptor
  d->addr   = (uint64)m->head;
  d->length = m->len;
  d->cmd    = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
  d->status = 0;
  tx_mbufs[tdt % TX_RING_SIZE] = m;

  // Tăng TDT để báo cho hardware có descriptor mới
  regs[E1000_TDT / 4] = (tdt + 1) % TX_RING_SIZE;

  release(&e1000_lock);
  return 0;
}

// ─── e1000_recv: Nhận gói tin từ DMA RX Ring (gọi khi có ngắt) ──────────────
void
e1000_recv(void)
{
  for (;;) {
    // Tính index descriptor tiếp theo sau tail
    uint32 rdt = regs[E1000_RDT / 4];
    uint32 next = (rdt + 1) % RX_RING_SIZE;
    struct rx_desc *d = &rx_ring[next];

    // Kiểm tra hardware đã điền dữ liệu vào descriptor chưa (cờ DD)
    if (!(d->status & E1000_RXD_STAT_DD))
      break; // Không còn gói tin mới

    // Đặt chiều dài cho mbuf và truyền lên tầng L2
    struct mbuf *m = rx_mbufs[next];
    m->len = d->length;

    // Cấp phát mbuf mới thay thế cho descriptor này
    struct mbuf *newm = mbufalloc(0);
    if (newm) {
      rx_ring[next].addr   = (uint64)newm->head;
      rx_ring[next].status = 0;
      rx_mbufs[next]       = newm;
    } else {
      // Nếu không cấp được mbuf mới: dùng lại mbuf cũ, bỏ gói này
      rx_ring[next].status = 0;
      regs[E1000_RDT / 4] = next;
      continue;
    }

    // Cập nhật RDT để báo cho hardware descriptor này đã được xử lý
    regs[E1000_RDT / 4] = next;

    // Gửi lên tầng Ethernet (không giữ lock khi gọi eth_input để tránh deadlock)
    eth_input(m);
  }
}

// ─── e1000_intr: Xử lý ngắt card mạng E1000 ────────────────────────────────
void
e1000_intr(void)
{
  // Đọc và xóa nguyên nhân ngắt qua thanh ghi ICR
  uint32 icr = regs[E1000_ICR / 4];

  if (icr & E1000_ICR_RXT0) {
    // Ngắt nhận gói tin — xử lý toàn bộ RX ring
    e1000_recv();
  }
}
