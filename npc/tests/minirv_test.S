.section .text
.globl _start
_start:
  addi  x1,  x0, 0x100   # x1 = 0x100
  addi  x2,  x0, 0x37    # x2 = 55
  sw    x2,  0(x1)       # mem[0x100] = 55
  lw    x3,  0(x1)       # x3 = 55
  sb    x2,  3(x1)       # mem[0x103] = 0x37
  lbu   x4,  3(x1)       # x4 = 55
  add   x5,  x3, x4      # x5 = 110
  lui   x6,  0x10000     # x6 = 0x10000000
  addi  x7,  x6, -1      # x7 = 0x0fffffff
  jalr  x8,  x0, 0x30    # x8 = link(0x28), jump to 0x30
  addi  x5,  x0, 0       # (skipped) would zero x5
  ebreak                 # (skipped)
  addi  x10, x5, 0       # a0 = x5 = 110
  ebreak
