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
//
typedef enum {
    NPC_RUNNING,
    NPC_STOP,     // after N steps / watchpoint hit
    NPC_END,      // ebreak (normal halt)
    NPC_TIMEOUT,  // exceeded MAX_CYCLES without ebreak
    NPC_QUIT,     // 'q'
    NPC_ABORT,    // difftest mismatch
} npc_state_t;
extern npc_state_t npc_state;
// single entry point to stop the CPU: records state + halt context (pc, a0)
void set_npc_state(npc_state_t state, uint32_t pc, int ret);

// ==================== register / memory ====================
// read GPR
uint32_t cpu_gpr(int idx);
// read PC
uint32_t cpu_pc(void);
// read 4 bytes, 0 if out of range
uint32_t sdb_mem_read(uint32_t addr);

// ==================== memory / device ====================
// load raw image into pmem, return bytes read (exits on open failure)
size_t pmem_load_image(const char *path);
// base address of pmem (difftest sync)
void *pmem_base(void);
// write one ebreak instruction at addr (tests with standalone halt symbol)
void place_ebreak(uint32_t addr);
// reset RTC boot timestamp (called once at startup)
void timer_set_boot(void);
// print out-of-bounds access counters (called by report_halt)
void pmem_print_oob_stats(void);

// ==================== trace ====================
// open itrace + mtrace + ftrace and init disassembler (elf_path == NULL -> no ftrace)
void trace_init(const char *elf_path);
// close the three trace files + capstone (called by report_halt, after iring replay)
void trace_close(void);
// record one executed instruction: push ringbuf + write itrace
void trace_record(uint32_t pc, uint32_t inst);
// log ftrace call/ret (step calls it after posedge)
void trace_ftrace(uint32_t pc_exec, uint32_t inst_exec, uint32_t next_pc);
// dump recent instructions (bad trap)
void trace_dump_iring(void);
// log one data-memory access (called by pmem)
void mtrace_log(char rw, uint32_t addr, uint32_t data, char wmask);

// ==================== execution engine ====================
// TRACE_OFF env: skip waveform + itrace (main sets, step reads)
extern bool trace_off;
// interactive sdb mode: suppress batch progress print (main sets, step reads)
extern bool sdb_mode;
// build DUT + waveform and assert reset (main calls once)
void init_sim(void);
// close waveform + free DUT (main calls once at shutdown)
void fini_sim(void);
// print trap report and return process exit code
int report_halt(void);

// ==================== difftest ====================
// 动态链接 NEMU 的 .so 并同步镜像/初始寄存器 (ref_so_file == NULL 表示不启用)
void init_difftest(const char *ref_so_file, void *img, size_t img_size);
// 每执行一条指令后调用: 让 REF 执行同一条指令并比对寄存器, 不一致则置 NPC_ABORT
void difftest_step(uint32_t pc_exec);

// ==================== sdb ====================
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
