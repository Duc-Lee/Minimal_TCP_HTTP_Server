// ============================================================================
// Thành viên 1 (TV1): Hạ tầng Mạng, Driver L2/L3 & Zero-Copy
// File: kernel/checksum.c
// Nhiệm vụ: Hàm tính Checksum RFC 1071 cho IP và TCP Pseudo Header
// ============================================================================

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "net.h"

// Thuật toán Internet Checksum RFC 1071 chuẩn
uint16
in_cksum(const void *buf, int len)
{
  const uint8 *data = (const uint8 *)buf;
  uint32 sum = 0;

  while (len > 1) {
    sum += ((uint16)data[0] << 8) | (uint16)data[1];
    data += 2;
    len -= 2;
  }

  // Nếu độ dài lẻ, cộng byte cuối cùng (padded với 0 ở byte thấp)
  if (len == 1) {
    sum += (uint16)data[0] << 8;
  }

  // Gập tổng 32-bit thành 16-bit
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  return htons((uint16)(~sum));
}

// Tính TCP checksum kèm Pseudo Header
uint16
tcp_checksum(struct mbuf *m, uint32 src_ip, uint32 dst_ip)
{
  uint32 sum = 0;
  uint8 ph[12];
  uint32 s = htonl(src_ip);
  uint32 d = htonl(dst_ip);
  uint16 len16 = htons(m->len);

  memmove(&ph[0], &s, 4);
  memmove(&ph[4], &d, 4);
  ph[8] = 0;
  ph[9] = IP_PROTO_TCP;
  memmove(&ph[10], &len16, 2);

  for (int i = 0; i < 12; i += 2) {
    sum += ((uint16)ph[i] << 8) | (uint16)ph[i + 1];
  }

  // Cộng tiếp checksum từ TCP header và payload
  const uint8 *data = (const uint8 *)m->head;
  int len = m->len;
  while (len > 1) {
    sum += ((uint16)data[0] << 8) | (uint16)data[1];
    data += 2;
    len -= 2;
  }

  if (len == 1) {
    sum += (uint16)data[0] << 8;
  }

  while (sum >> 16)
    sum = (sum & 0xffff) + (sum >> 16);

  return htons((uint16)(~sum));
}

