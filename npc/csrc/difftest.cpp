// csrc/difftest.cpp —— DiffTest (DUT 侧)
// 通过 dlopen 动态链接 NEMU 编译出的共享库 (riscv32-nemu-interpreter-so),
// 用其中的 difftest_* API 逐条指令比对 NPC(DUT) 与 NEMU(REF) 的寄存器状态。
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "npc.h"

// 与 NEMU 的 include/difftest-def.h 保持一致
enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

// 程序基址 (与 main.cpp 的 PMEM_BASE / NEMU 的 RESET_VECTOR 一致)
#define RESET_VECTOR 0x80000000u

// riscv32 寄存器 ABI 名 (仅用于报错信息, 与 reg.cpp 一致)
static const char *regs[] = {
  "$0", "ra", "sp", "gp", "tp", "t0", "t1", "t2", "s0", "s1", "a0",
  "a1", "a2", "a3", "a4", "a5", "a6", "a7", "s2", "s3", "s4", "s5",
  "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"
};

// NEMU 导出的 REF API 函数指针
static void (*ref_difftest_memcpy)(uint32_t addr, void *buf, size_t n, bool direction) = NULL;
static void (*ref_difftest_regcpy)(void *dut, bool direction) = NULL;
static void (*ref_difftest_exec)(uint64_t n) = NULL;

static bool difftest_enabled = false;

// 把 DUT(NPC) 当前的寄存器状态 (32 个 GPR + pc) 打包成 33 个 word。
// 布局与 NEMU 的 CPU_state { gpr[32]; pc; } 一致。
static void dut_regcpy(uint32_t regs[33]) {
  for (int i = 0; i < 32; i++) regs[i] = cpu_gpr(i);
  regs[32] = cpu_pc();
}

void init_difftest(const char *ref_so_file, void *img, size_t img_size) {
  if (ref_so_file == NULL) return;

  void *handle = dlopen(ref_so_file, RTLD_LAZY);
  if (handle == NULL) {
    fprintf(stderr, "difftest: dlopen(\"%s\") failed: %s\n", ref_so_file, dlerror());
    exit(1);
  }

  ref_difftest_memcpy = (void (*)(uint32_t, void*, size_t, bool))dlsym(handle, "difftest_memcpy");
  ref_difftest_regcpy = (void (*)(void*, bool))dlsym(handle, "difftest_regcpy");
  ref_difftest_exec   = (void (*)(uint64_t))dlsym(handle, "difftest_exec");
  void (*ref_difftest_init)(int) = (void (*)(int))dlsym(handle, "difftest_init");

  if (!ref_difftest_memcpy || !ref_difftest_regcpy || !ref_difftest_exec || !ref_difftest_init) {
    fprintf(stderr, "difftest: missing symbol in \"%s\": %s\n", ref_so_file, dlerror());
    exit(1);
  }

  ref_difftest_init(0);

  // 把程序镜像与初始寄存器状态同步到 REF
  ref_difftest_memcpy(RESET_VECTOR, img, img_size, DIFFTEST_TO_REF);
  uint32_t regs[33];
  dut_regcpy(regs);
  ref_difftest_regcpy(regs, DIFFTEST_TO_REF);

  difftest_enabled = true;
  printf("Differential testing: ON (ref = %s)\n", ref_so_file);
}

// 每执行一条指令后调用: 让 REF 执行同一条指令, 再逐项比较 32 个 GPR + pc。
// 一旦不一致, 置 npc_state = NPC_ABORT 停止执行 (由 cpu_exec 检查)。
void difftest_step(uint32_t pc_exec) {
  if (!difftest_enabled) return;

  ref_difftest_exec(1);

  uint32_t ref_regs[33];
  ref_difftest_regcpy(ref_regs, DIFFTEST_TO_DUT);

  uint32_t dut_regs[33];
  dut_regcpy(dut_regs);

  bool ok = true;

  if (ref_regs[32] != dut_regs[32]) {
    printf("difftest: pc mismatch after instruction at pc = 0x%08x\n"
           "  ref = 0x%08x, dut = 0x%08x\n",
           pc_exec, ref_regs[32], dut_regs[32]);
    ok = false;
  }

  // x0 恒为 0, 跳过
  for (int i = 1; i < 32; i++) {
    if (ref_regs[i] != dut_regs[i]) {
      printf("difftest: %s mismatch after instruction at pc = 0x%08x\n"
             "  ref = 0x%08x, dut = 0x%08x, diff = 0x%08x\n",
             regs[i], pc_exec, ref_regs[i], dut_regs[i], ref_regs[i] ^ dut_regs[i]);
      ok = false;
    }
  }

  if (!ok) npc_state = NPC_ABORT;
}
