#ifndef __NPC_H__
#define __NPC_H__

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "Vtop.h"

// ==================== DUT ====================
// Verilator top model, instantiated in main.cpp.
// sdb reads RTL signals via top->rootp->... (see cpu_gpr).
extern Vtop *top;

// ==================== execution state ====================
// Why execution stopped (like NEMU's nemu_state).
typedef enum {
    NPC_RUNNING,  // running
    NPC_STOP,     // stopped after N steps / watchpoint hit
    NPC_END,      // hit ebreak
    NPC_QUIT,     // user quit via 'q'
    NPC_ABORT,    // difftest 发现状态不一致
} npc_state_t;
extern npc_state_t npc_state;

// ==================== register / memory ====================
// read GPR idx (x0 always 0)
uint32_t cpu_gpr(int idx);
// read PC
uint32_t cpu_pc(void);
// read 4 bytes (little-endian), 0 if out of range
uint32_t sdb_mem_read(uint32_t addr);

// ==================== difftest ====================
// 动态链接 NEMU 的 .so 并同步镜像/初始寄存器 (ref_so_file == NULL 表示不启用)
void init_difftest(const char *ref_so_file, void *img, size_t img_size);
// 每执行一条指令后调用: 让 REF 执行同一条指令并比对寄存器, 不一致则置 NPC_ABORT
void difftest_step(uint32_t pc_exec);

// ==================== sdb ====================
// compile regex (expr.cpp)
void init_regex(void);
// init watchpoint pool (watchpoint.cpp)
void init_wp_pool(void);
// init_regex + init_wp_pool
void init_sdb(void);
// interactive command loop
void sdb_mainloop(void);
// run n instructions (UINT64_MAX = until stop)
npc_state_t cpu_exec(uint64_t n);

// ==================== expression ====================
// evaluate expression
uint32_t expr(char *e, bool *success);

// ==================== watchpoint ====================
typedef struct watchpoint {
    int NO;
    struct watchpoint *next;
    char expr[256];
    uint32_t old_val;
    uint32_t new_val;  // latest value on hit
} WP;
WP *new_wp(char *e);
void free_wp(int NO);
void wp_display(void);
// called after each instruction
bool wp_check(void);
// most-recent hit number (consumed), -1 if none
int wp_hit_no(void);

// ==================== register display / name lookup ====================
void isa_reg_display(void);
// name -> value
bool isa_reg_str2val(const char *s, uint32_t *val);

#endif
