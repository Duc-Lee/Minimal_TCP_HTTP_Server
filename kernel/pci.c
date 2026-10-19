#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"
#include "net.h"

void
pci_init(void)
{
  for (int dev = 0; dev < 32; dev++) {
    volatile uint32 *base = (volatile uint32 *)(0x30000000L + (dev << 15));
    uint32 id = base[0];
    if (id != 0xffffffff && id != 0) {
      printk("PCI found dev %d: vendor/dev=0x%x\n", dev, id);
      if ((id & 0xffff) == 0x8086) { // Intel e1000 (0x100e8086)
        printk("Configuring E1000 on dev %d...\n", dev);
        base[4] = 0x40000000;
        base[1] |= 7; // enable PCI bus master, memory space, I/O space
        e1000_init((uint32 *)0x40000000L);
        return;
      }
    }
  }
  printk("pci_init: e1000 NOT FOUND!\n");
}
