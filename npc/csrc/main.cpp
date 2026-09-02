#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <sys/time.h>
#include <time.h>
#include <verilated.h>
#include <verilated_fst_c.h>
#include <capstone/capstone.h>
#include "Vtop.h"
#include "Vtop___024root.h"   // 完整定义 (Vtop.h 里只有前向声明, 读内部信号需要它)
#include "npc.h"

// 存储器: 按字节寻址, 程序从 0x80000000 开始 (minirv-npc 运行时约定)
// pmem 数组下标 = 访存地址 - PMEM_BASE; 读写按参考 minirvEMU.c 的 ram_read32/ram_write32
#define PMEM_BASE 0x80000000u
#define PMEM_SIZE (16u << 20)   // 16MB
static uint8_t pmem[PMEM_SIZE] = {0};

// MMIO 设备地址 (与后续 SoC 一致)
#define SERIAL_ADDR 0x10000000   // 串口
#define RTC_ADDR    0x20000000   // 时钟 (RTC): offset 0/4=uptime 低/高, 8~28=年月日时分秒

// ==================== 全局仿真状态 ====================
Vtop *top = NULL;                       // DUT 顶层 (sdb 读寄存器也依赖它)
npc_state_t   npc_state = NPC_RUNNING;          // 执行状态, 见 npc.h 的 enum
static bool sdb_mode = false;           // 是否交互式调试 (抑制批处理的进度打印)

// 时钟基于系统时间: boot_us 记录仿真启动时刻, uptime = now - boot_us
static uint64_t boot_us = 0;
static uint64_t now_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return (uint64_t)tv.tv_sec * 1000000ull + tv.tv_usec;
}
static uint64_t uptime_us(void) { return now_us() - boot_us; }

// 结果输出用 ANSI 颜色 (模仿 NEMU)
#define ANSI_FG_GREEN "\33[1;32m"
#define ANSI_FG_RED   "\33[1;31m"
#define ANSI_NONE     "\33[0m"

static inline uint32_t addr_to_off(uint32_t addr) {
  return addr - PMEM_BASE;
}

// ==================== mtrace (访存踪迹) ====================
// 记录数据访存 (lw/lbu/sw/sb) 的踪迹; 取指不记 (指令踪迹已由 itrace 覆盖)。
// NPC 没有 Kconfig, 用环境变量代替 menuconfig 开关:
//   MTRACE=1            打开访存踪迹, 写 npc-mtrace.txt
//   MTRACE_RANGE=lo,hi  只记录地址落在 [lo, hi] 的访存 (十进制/十六进制均可)
static FILE *mtrace_fp = NULL;
static bool    mtrace_enable    = false;
static bool    mtrace_has_range = false;
static uint32_t mtrace_lo = 0, mtrace_hi = 0;

static void mtrace_init(void) {
  const char *e = getenv("MTRACE");
  mtrace_enable = (e != NULL && e[0] == '1');
  if (!mtrace_enable) return;

  const char *r = getenv("MTRACE_RANGE");
  if (r != NULL) {
    char *end = NULL;
    uint32_t lo = (uint32_t)strtoul(r, &end, 0);
    if (end != NULL && *end == ',') {
      mtrace_lo = lo;
      mtrace_hi = (uint32_t)strtoul(end + 1, NULL, 0);
      mtrace_has_range = true;
    }
  }

  mtrace_fp = fopen("npc-mtrace.txt", "w");
  if (mtrace_fp == NULL) {
    fprintf(stderr, "mtrace: cannot open npc-mtrace.txt\n");
    mtrace_enable = false;
  }
}

static void mtrace_log(char rw, uint32_t addr, uint32_t data, char wmask) {
  if (!mtrace_enable) return;
  if (mtrace_has_range && (addr < mtrace_lo || addr > mtrace_hi)) return;
  if (rw == 'r')
    fprintf(mtrace_fp, "read  0x%08x, len=4, data=0x%08x\n", addr, data);
  else
    fprintf(mtrace_fp, "write 0x%08x, len=4, data=0x%08x, wmask=0x%02x\n",
            addr, data, (unsigned)(unsigned char)wmask);
}

// 32 位总线读: 返回 addr 处连续的 4 字节 (小端), 越界返回 0。
// 公开给 sdb (x 命令 / 表达式解引用 *addr) 使用。
uint32_t sdb_mem_read(uint32_t addr) {
  if (addr < PMEM_BASE) return 0;               // 越界(如复位前的 X 地址)返回 0
  uint32_t base = addr_to_off(addr);
  if (base + 3 >= PMEM_SIZE) return 0;
  return (uint32_t)pmem[base + 0] |
         ((uint32_t)pmem[base + 1] << 8) |
         ((uint32_t)pmem[base + 2] << 16) |
         ((uint32_t)pmem[base + 3] << 24);
}

// 时钟设备读: offset 0/4 = uptime(us, 低/高 32 位), 8~28 = 年月日时分秒 (GMT)
static uint64_t rtc_latch = 0;   // 读高字时定格, 避免 64 位撕裂读
static int rtc_read(uint32_t offset) {
  if (offset == 4) { rtc_latch = uptime_us(); return (int32_t)(rtc_latch >> 32); }
  if (offset == 0) { return (int32_t)rtc_latch; }
  time_t now = time(NULL);
  struct tm *t = gmtime(&now);
  switch (offset) {
    case 8:  return t->tm_year + 1900;
    case 12: return t->tm_mon + 1;
    case 16: return t->tm_mday;
    case 20: return t->tm_hour;
    case 24: return t->tm_min;
    case 28: return t->tm_sec;
    default: return 0;
  }
}

// DPI-C: 数据总线读 (lsu 的 lw/lbu), 记 mtrace
extern "C" int pmem_read(int raddr) {
  uint32_t addr = (uint32_t)raddr;
  if (addr >= RTC_ADDR && addr < RTC_ADDR + 32) return rtc_read(addr - RTC_ADDR);
  uint32_t data = sdb_mem_read(addr);
  mtrace_log('r', addr, data, 0);
  return (int32_t)data;
}

// DPI-C: 取指 (ifu.v), 与数据访存分开, 不记 mtrace
extern "C" int pmem_ifetch(int raddr) {
  return (int32_t)sdb_mem_read((uint32_t)raddr);
}

// DPI-C: 数据总线写 (lsu 的 sw/sb), 按写掩码 wmask 写入 addr 处连续 4 字节, 记 mtrace
extern "C" void pmem_write(int waddr, int wdata, char wmask) {
  uint32_t addr = (uint32_t)waddr;
  if (addr == SERIAL_ADDR) {                        // 串口: 输出一个字符
    putchar((unsigned char)(wdata & 0xff));
    fflush(stdout);
    return;
  }
  mtrace_log('w', addr, (uint32_t)wdata, wmask);   // 先记踪迹(含越界地址)
  if (addr < PMEM_BASE) return;
  uint32_t base = addr_to_off(addr);
  if (base + 3 >= PMEM_SIZE) return;
  for (int i = 0; i < 4; i++) {
    if (wmask & (1 << i)) pmem[base + i] = (uint8_t)((wdata >> (i * 8)) & 0xff);
  }
}

// DPI-C: NPC 执行 ebreak 指令时被调用, 置位标志通知仿真环境结束仿真
static bool ebreak_triggered = false;
extern "C" void ebreak() { ebreak_triggered = true; }

// ==================== 寄存器访问 (sdb 用) ====================
// 直接读 Verilator 生成的内部变量: 寄存器堆 rf 在 top->rootp-> 下。
// 变量名形如 top__DOT__u_rf__DOT__rf, 与 RTL 的模块实例名/变量名有关,
// 修改 RTL 或升级 Verilator 时可能需要同步改这一个函数。
uint32_t cpu_gpr(int idx) {
  if (idx == 0) return 0;   // x0 恒为 0 (RTL 读口对 x0 特判, 直接读 rf[0] 会拿到脏值)
  return (uint32_t)top->rootp->top__DOT__u_rf__DOT__rf[idx];
}
uint32_t cpu_pc(void) {
  return (uint32_t)top->pc;
}

// ==================== itrace / iringbuf (踪迹) ====================

// 反汇编: 用 capstone 库 (tools/capstone/), 支持完整 RISC-V 指令集
// (不再手写只认 9 条 minirv 指令, 将来扩展指令集不用改这里)
static csh cs_handle;

static void init_disasm(void) {
  // RISCV32 + 压缩指令扩展 (RVC), 与 NEMU 的 riscv32 模式一致
  if (cs_open(CS_ARCH_RISCV, (cs_mode)(CS_MODE_RISCV32 | CS_MODE_RISCVC), &cs_handle) != CS_ERR_OK) {
    fprintf(stderr, "capstone: cs_open failed\n");
    exit(1);
  }
}

// 反汇编一条指令, 输出 "mnemonic\toperands" 到 buf (失败输出 unknown)
static void disasm(uint32_t pc, uint32_t inst, char *buf, size_t bufsz) {
  uint8_t code[4];
  memcpy(code, &inst, 4);   // 小端: code[0] = inst 最低字节 = 第一条指令字节

  cs_insn *insn;
  size_t count = cs_disasm(cs_handle, code, sizeof(code), pc, 1, &insn);
  if (count > 0) {
    if (insn[0].op_str[0] != '\0')
      snprintf(buf, bufsz, "%s\t%s", insn[0].mnemonic, insn[0].op_str);
    else
      snprintf(buf, bufsz, "%s", insn[0].mnemonic);
    cs_free(insn, count);
  } else {
    snprintf(buf, bufsz, "unknown");
  }
}

// 指令环形缓冲区: 记录最近 IRINGBUF_SIZE 条已执行指令, 出错时回放现场
#define IRINGBUF_SIZE 16
typedef struct { uint32_t pc; uint32_t inst; } iring_entry;
static iring_entry iringbuf[IRINGBUF_SIZE];
static int iring_head = 0;   // 下一个写入位置
static int iring_cnt  = 0;   // 已写入条数 (满后保持 IRINGBUF_SIZE)

static void trace_record(uint32_t pc, uint32_t inst) {
  iringbuf[iring_head].pc   = pc;
  iringbuf[iring_head].inst = inst;
  iring_head = (iring_head + 1) % IRINGBUF_SIZE;
  if (iring_cnt < IRINGBUF_SIZE) iring_cnt++;
}

// itrace: 把每条指令写进 npc-log.txt (PC / 反汇编 / 机器码)
static FILE *itrace_fp = NULL;
static void itrace_log(uint32_t pc, uint32_t inst) {
  char buf[64];
  disasm(pc, inst, buf, sizeof(buf));
  fprintf(itrace_fp, "0x%08x: %-24s  %02x %02x %02x %02x\n",
          pc, buf,
          (inst >> 24) & 0xff, (inst >> 16) & 0xff,
          (inst >> 8)  & 0xff,  inst        & 0xff);
}

// 出错/停机时回放 iringbuf, 最后一条(触发指令)用 --> 标记
static void iringbuf_dump(void) {
  if (iring_cnt == 0) return;
  printf("\n================ iringbuf ================\n");
  for (int i = 0; i < iring_cnt; i++) {
    int idx = (iring_head - iring_cnt + i + IRINGBUF_SIZE) % IRINGBUF_SIZE;
    char buf[64];
    disasm(iringbuf[idx].pc, iringbuf[idx].inst, buf, sizeof(buf));
    printf("%s 0x%08x: %-24s  %02x %02x %02x %02x\n",
           (i == iring_cnt - 1) ? "-->" : "   ",
           iringbuf[idx].pc, buf,
           (iringbuf[idx].inst >> 24) & 0xff, (iringbuf[idx].inst >> 16) & 0xff,
           (iringbuf[idx].inst >> 8)  & 0xff,  iringbuf[idx].inst        & 0xff);
  }
  printf("==========================================\n");
}

// ==================== ftrace (函数调用踪迹) ====================
// 从 ELF 的 .symtab + .strtab 手工解析出「地址 -> 函数名」映射, 追踪调用/返回。
// riscv32e 调用约定: 调用用 `jal ra,target`(近距离) 或 `jalr ra`(间接, 目标在寄存器);
// 返回用 `ret`(=jalr x0,0(x1))。因此:
//   call = jal/jalr 且 rd==x1(ra);   ret = 0x00008067
// 还有 jal/jalr x0,0(xN) 是尾跳转(不存返回地址), 不算 call 也不算 ret, 直接忽略。
static FILE *ftrace_fp = NULL;
static int   ftrace_depth = 0;

typedef struct { uint32_t start; uint32_t size; const char *name; } ftrace_func;
static ftrace_func *ftrace_funcs = NULL;
static size_t       ftrace_nfuncs = 0;
static char        *ftrace_elfbuf = NULL;   // 整个 ELF 文件缓冲 (name 指针指向其中)

// 手工解析 ELF32: ELF header -> section header table -> .symtab + .strtab
static bool ftrace_load(const char *elf_path) {
  FILE *f = fopen(elf_path, "rb");
  if (!f) { fprintf(stderr, "ftrace: cannot open %s\n", elf_path); return false; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  ftrace_elfbuf = (char*)malloc((size_t)sz);
  if (fread(ftrace_elfbuf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return false; }
  fclose(f);

  Elf32_Ehdr *eh = (Elf32_Ehdr*)ftrace_elfbuf;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32) {
    fprintf(stderr, "ftrace: %s is not a 32-bit ELF\n", elf_path);
    return false;
  }

  Elf32_Shdr *sh = (Elf32_Shdr*)(ftrace_elfbuf + eh->e_shoff);
  Elf32_Shdr *symtab = NULL;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (sh[i].sh_type == SHT_SYMTAB) { symtab = &sh[i]; break; }
  }
  if (!symtab) { fprintf(stderr, "ftrace: no .symtab\n"); return false; }

  Elf32_Shdr *strtab = &sh[symtab->sh_link];   // .symtab 的 sh_link 指向 .strtab
  const char *strs = ftrace_elfbuf + strtab->sh_offset;
  Elf32_Sym  *syms = (Elf32_Sym*)(ftrace_elfbuf + symtab->sh_offset);
  int nsym = (int)(symtab->sh_size / sizeof(Elf32_Sym));

  // 只收集 Type=FUNC 且地址非 0 的表项
  ftrace_nfuncs = 0;
  for (int i = 0; i < nsym; i++)
    if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_value != 0)
      ftrace_nfuncs++;

  ftrace_funcs = (ftrace_func*)malloc(ftrace_nfuncs * sizeof(ftrace_func));
  size_t k = 0;
  for (int i = 0; i < nsym; i++) {
    if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_value != 0) {
      ftrace_funcs[k].start = syms[i].st_value;
      ftrace_funcs[k].size  = syms[i].st_size;
      ftrace_funcs[k].name  = strs + syms[i].st_name;   // Name 字段是到 .strtab 的偏移
      k++;
    }
  }
  return true;
}

// 逐项扫描 FUNC, 地址落在 [start, start+size) 内则返回函数名, 否则 "???"
static const char *ftrace_name(uint32_t addr) {
  for (size_t i = 0; i < ftrace_nfuncs; i++)
    if (addr >= ftrace_funcs[i].start && addr < ftrace_funcs[i].start + ftrace_funcs[i].size)
      return ftrace_funcs[i].name;
  return "???";
}

static bool ftrace_is_call(uint32_t inst) {
  return ((inst & 0x7f) == 0x6f || (inst & 0x7f) == 0x67) && ((inst >> 7) & 0x1f) == 1;   // jal/jalr rd=x1(ra)
}
static bool ftrace_is_ret(uint32_t inst) {
  return inst == 0x00008067;                                    // jalr x0, 0(x1) = ret
}

static void ftrace_call(uint32_t pc, uint32_t target) {
  fprintf(ftrace_fp, "0x%08x: %*scall [%s@0x%08x]\n",
          pc, ftrace_depth * 2, "", ftrace_name(target), target);
  ftrace_depth++;
}
static void ftrace_ret(uint32_t pc) {
  if (ftrace_depth > 0) ftrace_depth--;
  fprintf(ftrace_fp, "0x%08x: %*sret  [%s]\n",
          pc, ftrace_depth * 2, "", ftrace_name(pc));
}

// ==================== 执行引擎 ====================
static VerilatedFstC *tfp = NULL;
static uint64_t cycle = 0;
static bool trace_off = false;
static const uint64_t MAX_CYCLES = 100000000;

// 单步执行一条指令 (一个时钟周期)
static void step(void) {
  top->clk = 0; top->eval();

  // 本周期执行的指令: posedge 之前 pc/inst 就是当拍取指结果
  uint32_t pc_exec   = (uint32_t)top->pc;
  uint32_t inst_exec = (uint32_t)top->inst;
  trace_record(pc_exec, inst_exec);
  if (itrace_fp) itrace_log(pc_exec, inst_exec);

  top->clk = 1; top->eval();

  // ftrace: posedge 后 pc 已是本指令执行后的目标地址 (call 的目标)
  if (ftrace_fp) {
    uint32_t next_pc = (uint32_t)top->pc;
    if (ftrace_is_call(inst_exec))     ftrace_call(pc_exec, next_pc);
    else if (ftrace_is_ret(inst_exec)) ftrace_ret(pc_exec);
  }

  // difftest: 让 REF (NEMU) 执行同一条指令并比对寄存器
  difftest_step(pc_exec);

  if (!trace_off) tfp->dump(cycle);
  if (!sdb_mode && cycle % 1000000 == 0)
    printf("... %llu instructions, pc = 0x%08x\n",
           (unsigned long long)cycle, pc_exec);
  cycle++;
}

// 跑 n 条指令; n == UINT64_MAX 表示一直跑到 ebreak / 监视点 / 超时为止
npc_state_t cpu_exec(uint64_t n) {
  npc_state = NPC_RUNNING;
  for (uint64_t i = 0; i < n; i++) {
    step();
    if (npc_state == NPC_ABORT) break;                      // difftest 发现寄存器不一致
    if (ebreak_triggered)  { npc_state = NPC_END;  break; }
    if (wp_check())        { npc_state = NPC_STOP; break; }
    if (cycle >= MAX_CYCLES) { npc_state = NPC_END; break; }  // 兜底超时, 视为 bad trap
  }
  if (npc_state == NPC_RUNNING) npc_state = NPC_STOP;  // 正常执行完 n 条
  return npc_state;
}

// 收尾: 补记停机指令 + 打印 trap 报告, 返回进程退出码 (供 AM cpu-tests 判 PASS/FAIL)
static int report_halt(void) {
  uint32_t halt_pc = (uint32_t)top->pc;
  int halt_ret = (int)top->a0;

  // 停机指令 ebreak 被取指后立即停机, 没经历自己的执行周期, 这里补记一条到踪迹
  if (ebreak_triggered) {
    trace_record(halt_pc, (uint32_t)top->inst);
    if (itrace_fp) itrace_log(halt_pc, (uint32_t)top->inst);
  }

  if (itrace_fp) { fclose(itrace_fp); itrace_fp = NULL; }
  if (mtrace_fp) { fclose(mtrace_fp); mtrace_fp = NULL; }
  if (ftrace_fp) { fclose(ftrace_fp); ftrace_fp = NULL; }

  bool is_good = ebreak_triggered && halt_ret == 0;

  if (npc_state != NPC_QUIT) {   // 用户主动 q 退出时不打印 trap 报告
    if (!is_good) iringbuf_dump();   // 坏 trap / 超时: 回放最近指令现场

    if (npc_state == NPC_ABORT) {
      printf(ANSI_FG_RED "DIFFTEST FAIL" ANSI_NONE " at pc = 0x%08x\n", halt_pc);
    } else if (ebreak_triggered) {
      if (halt_ret == 0)
        printf(ANSI_FG_GREEN "HIT GOOD TRAP" ANSI_NONE " at pc = 0x%08x\n", halt_pc);
      else
        printf(ANSI_FG_RED   "HIT BAD TRAP"  ANSI_NONE " at pc = 0x%08x\n", halt_pc);
    } else {
      printf(ANSI_FG_RED "HIT BAD TRAP" ANSI_NONE " (max cycles, no ebreak) at pc = 0x%08x\n", halt_pc);
    }
    printf("  %llu instructions, halt code (a0) = %d\n", (unsigned long long)cycle, halt_ret);
  }

  // 返回码: 0 = good (a0 == 0) 或用户主动 q 退出, 非 0 = bad
  return (npc_state == NPC_QUIT) ? 0 : ((is_good) ? 0 : 1);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <image.bin> [halt_addr] [--elf <path>] [-d <ref.so>] [-s]\n", argv[0]);
    return 1;
  }

  // 解析可选参数:
  //   --elf <path> 为 ftrace 提供符号表
  //   -d / --diff <ref.so> 打开 DiffTest (动态链接 NEMU 的共享库)
  //   -s / --sdb   进入交互式调试器 (sdb)
  //   第一个非 '-' 选项参数 = halt_addr (兼容旧的 positional 用法)
  const char *elf_path = NULL;
  const char *halt_addr_str = NULL;
  const char *diff_so = NULL;
  sdb_mode = false;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--elf") == 0 && i + 1 < argc) elf_path = argv[++i];
    else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--diff") == 0) {
      if (i + 1 < argc) diff_so = argv[++i];
    }
    else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--sdb") == 0) sdb_mode = true;
    else if (halt_addr_str == NULL && argv[i][0] != '-') halt_addr_str = argv[i];
  }

  // 从文件读入程序镜像 (raw binary, 链接在 0x80000000)
  FILE *fp = fopen(argv[1], "rb");
  if (!fp) { perror("fopen"); return 1; }
  size_t nbytes = fread(pmem, 1, PMEM_SIZE, fp);
  fclose(fp);
  printf("loaded %zu bytes from %s\n", nbytes, argv[1]);

  // 可选: 在 halt_addr 处放置 ebreak (用于带独立 halt 符号的测试, 如 minirv-logisim)
  if (halt_addr_str) {
    uint32_t halt_addr = (uint32_t)strtoul(halt_addr_str, NULL, 0);
    pmem_write((int)halt_addr, 0x00100073, (char)0xf);   // ebreak
    printf("place ebreak at halt() = 0x%08x\n", halt_addr);
  }

  trace_off = getenv("TRACE_OFF") != NULL;
  Verilated::traceEverOn(true);
  top = new Vtop;
  tfp = new VerilatedFstC;
  top->trace(tfp, 99);
  tfp->open("dump.fst");

  // 复位
  top->rst = 1; top->clk = 0; top->eval();
  top->clk = 1; top->eval();       // posedge: pc <= 0x80000000
  top->rst = 0;
  top->eval();                     // 让取指组合逻辑稳定, 首次 si 前就能看到正确 inst
  ebreak_triggered = false;        // 复位取指若恰好是 ebreak 不计数

  // 打开 itrace 日志
  itrace_fp = trace_off ? NULL : fopen("npc-log.txt", "w");

  // 打开 mtrace 日志 (由 MTRACE 环境变量控制)
  mtrace_init();

  // 打开 ftrace 日志 (由 --elf 参数控制)
  if (elf_path && ftrace_load(elf_path)) {
    ftrace_fp = fopen("npc-ftrace.txt", "w");
    if (!ftrace_fp) fprintf(stderr, "ftrace: cannot open npc-ftrace.txt\n");
  }

  init_disasm();        // 初始化 capstone 反汇编

  boot_us = now_us();   // 记录仿真启动时刻, 供时钟设备计算 uptime
  init_sdb();           // 编译正则 + 初始化监视点池

  // 打开 DiffTest (diff_so 为 NULL 时内部直接返回, 不启用)
  init_difftest(diff_so, (void *)pmem, nbytes);

  if (sdb_mode) {
    sdb_mainloop();          // 交互式调试
  } else {
    cpu_exec(UINT64_MAX);    // 批处理: 一直跑到 ebreak (原行为)
  }

  int ret = report_halt();

  cs_close(&cs_handle);
  tfp->close(); top->final(); delete top; delete tfp;

  return ret;
}
