#ifndef _E1000_DEV_H_
#define _E1000_DEV_H_

#include "types.h"

// Intel 82540EP Gigabit Ethernet Controller register definitions & descriptors
// Reference: Intel 8254x Family Software Developer's Manual

#define E1000_CTL      0x00000 // Device Control
#define E1000_STATUS   0x00008 // Device Status
#define E1000_EECD     0x00010 // EEPROM/Flash Control/Data
#define E1000_EERD     0x00014 // EEPROM Read
#define E1000_CTRL_EXT 0x00018 // Extended Device Control
#define E1000_MDIC     0x00020 // MDI Control
#define E1000_ICR      0x000C0 // Interrupt Cause Read
#define E1000_ITR      0x000C4 // Interrupt Throttling Rate
#define E1000_ICS      0x000C8 // Interrupt Cause Set
#define E1000_IMS      0x000D0 // Interrupt Mask Set
#define E1000_IMC      0x000D8 // Interrupt Mask Clear
#define E1000_RCTL     0x00100 // Receive Control
#define E1000_TCTL     0x00400 // Transmit Control
#define E1000_TIPG     0x00410 // Transmit Inter-packet Gap
#define E1000_RDBAL    0x02800 // Rx Descriptor Base Address Low
#define E1000_RDBAH    0x02804 // Rx Descriptor Base Address High
#define E1000_RDLEN    0x02808 // Rx Descriptor Length
#define E1000_RDH      0x02810 // Rx Descriptor Head
#define E1000_RDT      0x02818 // Rx Descriptor Tail
#define E1000_TDBAL    0x03800 // Tx Descriptor Base Address Low
#define E1000_TDBAH    0x03804 // Tx Descriptor Base Address High
#define E1000_TDLEN    0x03808 // Tx Descriptor Length
#define E1000_TDH      0x03810 // Tx Descriptor Head
#define E1000_TDT      0x03818 // Tx Descriptor Tail
#define E1000_RA       0x05400 // Receive Address (MAC)
#define E1000_MTA      0x05200 // Multicast Table Array

// Control Register bits
#define E1000_CTL_SLU 0x00000040 // Set link up
#define E1000_CTL_RST 0x04000000 // Full device reset

// Receive Control bits
#define E1000_RCTL_EN      0x00000002 // Receiver Enable
#define E1000_RCTL_SBP     0x00000004 // Store Bad Packets
#define E1000_RCTL_UPE     0x00000008 // Unicast Promiscuous Enabled
#define E1000_RCTL_MPE     0x00000010 // Multicast Promiscuous Enabled
#define E1000_RCTL_LPE     0x00000020 // Long Packet Enable
#define E1000_RCTL_LBM_NO  0x00000000 // No Loopback
#define E1000_RCTL_BAM     0x00008000 // Broadcast Accept Mode
#define E1000_RCTL_SZ_2048 0x00000000 // Rx buffer size 2048 B
#define E1000_RCTL_SECRC   0x04000000 // Strip Ethernet CRC

// Transmit Control bits
#define E1000_TCTL_EN   0x00000002 // Transmit Enable
#define E1000_TCTL_PSP  0x00000008 // Pad Short Packets
#define E1000_TCTL_CT   0x00000ff0 // Collision Threshold
#define E1000_TCTL_COLD 0x003ff000 // Collision Distance

// Interrupt bits
#define E1000_ICR_TXDW   0x00000001 // Tx desc written back
#define E1000_ICR_TXQE   0x00000002 // Tx Queue empty
#define E1000_ICR_LSC    0x00000004 // Link Status Change
#define E1000_ICR_RXSEQ  0x00000008 // Rx sequence error
#define E1000_ICR_RXDMT0 0x00000010 // Rx desc min threshold
#define E1000_ICR_RXO    0x00000040 // Rx overrun
#define E1000_ICR_RXT0   0x00000080 // Rx timer intr

// Transmit Descriptor
struct tx_desc {
  uint64 addr;
  uint16 length;
  uint8 cso;
  uint8 cmd;
  uint8 status;
  uint8 css;
  uint16 special;
};

// Transmit Command bits
#define E1000_TXD_CMD_EOP  0x01 // End of Packet
#define E1000_TXD_CMD_IFCS 0x02 // Insert FCS/CRC
#define E1000_TXD_CMD_RS   0x08 // Report Status

// Transmit Status bits
#define E1000_TXD_STAT_DD 0x01 // Descriptor Done

// Receive Descriptor
struct rx_desc {
  uint64 addr;
  uint16 length;
  uint16 csum;
  uint8 status;
  uint8 errors;
  uint16 special;
};

// Receive Status bits
#define E1000_RXD_STAT_DD  0x01 // Descriptor Done
#define E1000_RXD_STAT_EOP 0x02 // End of Packet

#endif // _E1000_DEV_H_
