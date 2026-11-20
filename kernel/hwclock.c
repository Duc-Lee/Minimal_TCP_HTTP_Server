// dong ho phan cung do phan giai micro giay (tuan 5 - TV1)
//
// cung cap 2 nguon thoi gian cho tang mang va cac benchmark:
//   1. csr "time"  (CLINT mtime, 10 MHz co dinh tren QEMU virt): dung lam
//      dong ho thuc te don vi us -> do rtt / min_rtt / delivery rate cho BBR.
//   2. csr "cycle" (lenh rdcycle): dem chu ky xung nhip cpu -> do chi phi cpu
//      cua mot doan code (vd sys_sendfile so voi read()+write()).
//
// tan so cua cycle khong co dinh (qemu tcg lay theo bo dem cua may host) nen
// luc boot ta hieu chuan: dem so chu ky cpu trong HWCLOCK_CAL_US micro giay
// do bang dong ho time, roi luu ty le cycle/us duoi dang fixed-point x1000.

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"

#define HWCLOCK_CAL_US 20000 // hieu chuan trong 20 ms
#define HWCLOCK_FP     1000  // he so fixed-point cho ty le cycle/us

static uint64 cyc_per_us_fp; // so chu ky cpu tren 1 us, nhan HWCLOCK_FP
static uint64 boot_us;       // thoi diem hwclock_init() (us)

// hieu chuan cycle theo time. goi 1 lan tren hart 0 luc boot, truoc khi
// bat ngat (khong bi timer interrupt cat ngang vong do).
void
hwclock_init(void)
{
  uint64 t0, t1, c0, c1;
  uint64 span = HWCLOCK_CAL_US * TIMEBASE_PER_US;

  // cho canh len cua time de bat dau dung bien 100 ns
  t0 = r_time();
  while (r_time() == t0)
    ;

  t0 = r_time();
  c0 = r_cycle();
  while (r_time() - t0 < span)
    ;
  c1 = r_cycle();
  t1 = r_time();

  uint64 us = (t1 - t0) / TIMEBASE_PER_US;
  if (us == 0)
    us = 1;
  cyc_per_us_fp = ((c1 - c0) * HWCLOCK_FP) / us;
  if (cyc_per_us_fp == 0)
    cyc_per_us_fp = HWCLOCK_FP; // phong khi cycle khong dem: coi 1 cycle = 1 us

  boot_us = r_time_us();

  uint64 whole = cyc_per_us_fp / HWCLOCK_FP;
  uint64 frac = cyc_per_us_fp % HWCLOCK_FP;
  printk("hwclock: timebase=%lu Hz (%lu tick/us), cpu=%lu.%lu%lu%lu cycle/us "
         "(calib %lu us, %lu cycles)\n",
         (uint64)TIMEBASE_HZ, (uint64)TIMEBASE_PER_US, whole, frac / 100,
         (frac / 10) % 10, frac % 10, us, c1 - c0);
}

// thoi gian thuc hien tai (us) tinh tu luc reset may
uint64
hwclock_now_us(void)
{
  return r_time_us();
}

// so us da troi qua tu luc hwclock_init()
uint64
hwclock_uptime_us(void)
{
  return r_time_us() - boot_us;
}

// so chu ky cpu hien tai (rdcycle)
uint64
hwclock_cycles(void)
{
  return r_cycle();
}

// doi so chu ky cpu sang micro giay theo ty le da hieu chuan
uint64
hwclock_cycles_to_us(uint64 cycles)
{
  // cycles * 1000 chi tran khi cycles > 1.8e16 (~ vai ngay o 100 GHz) -> an toan
  return (cycles * HWCLOCK_FP) / cyc_per_us_fp;
}

// doi micro giay sang so chu ky cpu tuong ung
uint64
hwclock_us_to_cycles(uint64 us)
{
  return (us * cyc_per_us_fp) / HWCLOCK_FP;
}

// ty le cycle/us da hieu chuan (fixed-point x1000)
uint64
hwclock_cycles_per_us_x1000(void)
{
  return cyc_per_us_fp;
}
