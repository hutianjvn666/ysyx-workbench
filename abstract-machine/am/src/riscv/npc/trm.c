#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <riscv/riscv.h>

extern char _heap_start;
int main(const char *args);

extern char _pmem_start;
#define PMEM_SIZE (128 * 1024 * 1024)
#define PMEM_END  ((uintptr_t)&_pmem_start + PMEM_SIZE)

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

#define SERIAL_ADDR 0x10000000   // 与后续 SoC 串口地址一致

void putch(char ch) {
  outb(SERIAL_ADDR, ch);
}

void halt(int code) {
  asm volatile("mv a0, %0; ebreak" : : "r"(code));
  while (1);   // should not reach here
}

void _trm_init() {
  uint32_t mvendorid, marchid;
  asm volatile("csrr %0, mvendorid" : "=r"(mvendorid));
  asm volatile("csrr %0, marchid" : "=r"(marchid));
  printf("mvendorid = 0x%x\n", mvendorid);
  printf("marchid   = 0x%x\n", marchid);

  int ret = main(mainargs);
  halt(ret);
}
