#include "Vtop___024root.h"  // 完整定义 (Vtop.h 里只有前向声明, 读内部信号需要它)
#include "npc.h"
#include <stdint.h>
#include <stdio.h>
#include <verilated.h>
#include <verilated_fst_c.h>

// ==================== 执行引擎 ====================
// 驱动 Verilator 生成的 DUT (top), 逐周期执行, 并在关键节点挂接
// 踪迹 (trace) 与 DiffTest。寄存器读取 (cpu_gpr/cpu_pc) 也放在这,
// 因为它们直接读 RTL 内部信号。

// ---- DUT 与执行状态 (定义在此, npc.h 中 extern 声明) ----
Vtop *top = NULL;  // DUT 顶层 (sdb 读寄存器也依赖它)
npc_state_t npc_state = NPC_RUNNING;  // 执行状态, 见 npc.h 的 enum

// ---- 停机现场 (set_npc_state 在停机那一刻捕获, report_halt 读取) ----
static uint32_t halt_pc = 0;
static int halt_ret = 0;

void set_npc_state(npc_state_t state, uint32_t pc, int ret) {
    npc_state = state;
    halt_pc = pc;
    halt_ret = ret;
}

// ---- 全局配置 (main 设置, step 读取) ----
bool trace_off = false;
bool sdb_mode = false;

// ---- DUT 波形 / 周期 ----
static VerilatedFstC *tfp = NULL;
static uint64_t cycle = 0;
static const uint64_t MAX_CYCLES = 100000000;

#define ANSI_FG_GREEN "\33[1;32m"
#define ANSI_FG_RED "\33[1;31m"
#define ANSI_NONE "\33[0m"

// ==================== 寄存器访问 (sdb 用) ====================
// 直接读 Verilator 生成的内部变量: 寄存器堆 rf 在 top->rootp-> 下。
// 变量名形如 top__DOT__u_rf__DOT__rf, 与 RTL 的模块实例名/变量名有关,
// 修改 RTL 或升级 Verilator 时可能需要同步改这一个函数。
uint32_t cpu_gpr(int idx) {
    if (idx == 0)
        return 0;
    return (uint32_t)top->rootp->top__DOT__u_rf__DOT__rf[idx];
}
uint32_t cpu_pc(void) {
    return (uint32_t)top->pc;
}

// ==================== 单步执行 ====================
// 单步执行一条指令 (一个时钟周期)
static void step(void) {
    top->clk = 0;
    top->eval();

    // 本周期执行的指令: posedge 之前 pc/inst 就是当拍取指结果
    uint32_t pc_exec = (uint32_t)top->pc;
    uint32_t inst_exec = (uint32_t)top->inst;
    trace_record(pc_exec, inst_exec);

    top->clk = 1;
    top->eval();

    // ftrace: posedge 后 pc 已是本指令执行后的目标地址 (call 的目标)
    uint32_t next_pc = (uint32_t)top->pc;
    trace_ftrace(pc_exec, inst_exec, next_pc);

    // difftest: 让 REF (NEMU) 执行同一条指令并比对寄存器。
    // ebreak 是停机指令: 停机后 pc 语义两实现不一致 (NEMU=pc+4, NPC=pc),
    // 且 ebreak 无寄存器副作用, 跳过比对
    if (inst_exec != 0x00100073)
        difftest_step(pc_exec);

    if (!trace_off)
        tfp->dump(cycle);
    if (!sdb_mode && cycle % 1000000 == 0)
        printf("... %llu instructions, pc = 0x%08x\n",
               (unsigned long long)cycle, pc_exec);
    cycle++;
}

npc_state_t cpu_exec(uint64_t n) {
    npc_state = NPC_RUNNING;
    for (uint64_t i = 0; i < n; ++i) {
        step();
        // ebreak (NPC_END) / difftest (NPC_ABORT) 已在 step() 内部由 DPI-C 置位
        if (npc_state != NPC_RUNNING)
            break;
        if (wp_check()) {
            npc_state = NPC_STOP;
            break;
        }
        if (cycle >= MAX_CYCLES) {
            set_npc_state(NPC_TIMEOUT, (uint32_t)top->pc, (int)top->a0);
            break;
        }
    }
    if (npc_state == NPC_RUNNING)
        npc_state = NPC_STOP;
    return npc_state;
}

// 收尾: 补记停机指令 + 打印 trap 报告, 返回进程退出码 (供 AM cpu-tests 判
// PASS/FAIL)
int report_halt(void) {
    // halt_pc / halt_ret 已由 set_npc_state 在停机那一刻捕获 (无需再读 DUT 内部信号)

    if (npc_state == NPC_QUIT) {
        trace_close();
        return 0;
    }

    bool good = (npc_state == NPC_END) && halt_ret == 0;

    if (!good)
        trace_dump_iring();  // 坏 trap / 超时: 回放最近指令现场 (用 capstone, 须在 trace_close 前)

    trace_close();  // 关 itrace/mtrace/ftrace 文件 + capstone

    const char *msg = NULL;
    if (npc_state == NPC_ABORT)
        msg = ANSI_FG_RED "DIFFTEST FAIL" ANSI_NONE;
    else if (good)
        msg = ANSI_FG_GREEN "HIT GOOD TRAP" ANSI_NONE;
    else if (npc_state == NPC_END)
        msg = ANSI_FG_RED "HIT BAD TRAP" ANSI_NONE;
    else
        msg = ANSI_FG_RED "HIT BAD TRAP (max cycles, no ebreak)" ANSI_NONE;

    printf("%s at pc = 0x%08x\n", msg, halt_pc);
    printf("  %llu instructions, halt code (a0) = %d\n",
           (unsigned long long)cycle, halt_ret);
    pmem_print_oob_stats();

    // 返回码: 0 = good (a0 == 0), 非 0 = bad
    return good ? 0 : 1;
}

// ==================== DUT 生命周期 ====================
void init_sim(void) {
    Verilated::traceEverOn(true);
    top = new Vtop;
    tfp = new VerilatedFstC;
    top->trace(tfp, 99);
    tfp->open("dump.fst");

    top->rst = 1;
    top->clk = 0;
    top->eval();
    top->clk = 1;
    top->eval();  // posedge: pc <= 0x80000000
    top->rst = 0;
    top->eval();  // 让取指组合逻辑稳定, 首次 si 前就能看到正确 inst
}

void fini_sim(void) {
    tfp->close();
    top->final();
    delete top;
    delete tfp;
}
