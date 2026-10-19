// tinh internet checksum rfc 1071
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "net.h"

// tinh checksum rfc 1071 bu 1
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

  // xu ly byte le neu co
  if (len == 1) {
    sum += (uint16)data[0] << 8;
  }

  // gap tong 32-bit thanh 16-bit
  while (sum >> 16) {
    sum = (sum & 0xffff) + (sum >> 16);
  }

  return htons((uint16)(~sum));
}

// tinh tcp checksum kem pseudo header
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

  // cong tiep checksum tu tcp header va payload
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

