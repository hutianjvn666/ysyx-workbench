#include "npc.h"
#include <capstone/capstone.h>
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== mtrace ====================
static FILE *mtrace_fp = NULL;
static bool mtrace_enable = false;
static bool mtrace_has_range = false;
static uint32_t mtrace_lo = 0, mtrace_hi = 0;

static void mtrace_init(void) {
    const char *e = getenv("MTRACE");
    mtrace_enable = (e != NULL && e[0] == '1');
    if (!mtrace_enable)
        return;

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

static void mtrace_close(void) {
    if (mtrace_fp) {
        fclose(mtrace_fp);
        mtrace_fp = NULL;
    }
}

void mtrace_log(char rw, uint32_t addr, uint32_t data, char wmask) {
    if (!mtrace_enable)
        return;
    if (mtrace_has_range && (addr < mtrace_lo || addr > mtrace_hi))
        return;
    if (rw == 'r')
        fprintf(mtrace_fp, "read  0x%08x, len=4, data=0x%08x\n", addr, data);
    else
        fprintf(mtrace_fp, "write 0x%08x, len=4, data=0x%08x, wmask=0x%02x\n",
                addr, data, (unsigned)(unsigned char)wmask);
}

// ==================== capstone 反汇编 ====================
// 反汇编: 用 capstone 库 (tools/capstone/), 支持完整 RISC-V 指令集
static csh cs_handle;

static void disasm_init(void) {
    // RISCV32 + 压缩指令扩展 (RVC), 与 NEMU 的 riscv32 模式一致
    if (cs_open(CS_ARCH_RISCV, (cs_mode)(CS_MODE_RISCV32 | CS_MODE_RISCVC),
                &cs_handle) != CS_ERR_OK) {
        fprintf(stderr, "capstone: cs_open failed\n");
        exit(1);
    }
}

static void disasm_fini(void) {
    cs_close(&cs_handle);
}

// 反汇编一条指令, 输出 "mnemonic\toperands" 到 buf (失败输出 unknown)
static void disasm(uint32_t pc, uint32_t inst, char *buf, size_t bufsz) {
    uint8_t code[4];
    memcpy(code, &inst, 4);  // 小端: code[0] = inst 最低字节 = 第一条指令字节

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

// ==================== itrace / iringbuf ====================
#define IRINGBUF_SIZE 16
typedef struct {
    uint32_t pc;
    uint32_t inst;
} iring_entry;
static iring_entry iringbuf[IRINGBUF_SIZE];
static int iring_head = 0;
static int iring_cnt = 0;

static FILE *itrace_fp = NULL;

static void itrace_init(void) {
    itrace_fp = trace_off ? NULL : fopen("npc-log.txt", "w");
}

static void itrace_close(void) {
    if (itrace_fp) {
        fclose(itrace_fp);
        itrace_fp = NULL;
    }
}

static void itrace_log(uint32_t pc, uint32_t inst) {
    if (!itrace_fp)
        return;
    char buf[64];
    disasm(pc, inst, buf, sizeof(buf));
    fprintf(itrace_fp, "0x%08x: %-24s  %02x %02x %02x %02x\n", pc, buf,
            (inst >> 24) & 0xff, (inst >> 16) & 0xff, (inst >> 8) & 0xff,
            inst & 0xff);
}

void trace_record(uint32_t pc, uint32_t inst) {
    iringbuf[iring_head].pc = pc;
    iringbuf[iring_head].inst = inst;
    iring_head = (iring_head + 1) % IRINGBUF_SIZE;
    if (iring_cnt < IRINGBUF_SIZE)
        iring_cnt++;
    itrace_log(pc, inst);
}

void trace_dump_iring(void) {
    if (iring_cnt == 0)
        return;
    printf("\n================ iringbuf ================\n");
    for (int i = 0; i < iring_cnt; i++) {
        int idx = (iring_head - iring_cnt + i + IRINGBUF_SIZE) % IRINGBUF_SIZE;
        char buf[64];
        disasm(iringbuf[idx].pc, iringbuf[idx].inst, buf, sizeof(buf));
        printf("%s 0x%08x: %-24s  %02x %02x %02x %02x\n",
               (i == iring_cnt - 1) ? "-->" : "   ", iringbuf[idx].pc, buf,
               (iringbuf[idx].inst >> 24) & 0xff,
               (iringbuf[idx].inst >> 16) & 0xff,
               (iringbuf[idx].inst >> 8) & 0xff, iringbuf[idx].inst & 0xff);
    }
    printf("==========================================\n");
}

// ==================== ftrace (函数调用踪迹) ====================
// 从 ELF 的 .symtab + .strtab 手工解析出「地址 -> 函数名」映射, 追踪调用/返回。
// riscv32e 调用约定: 调用用 `jal ra,target`(近距离) 或 `jalr ra`(间接,
// 目标在寄存器); 返回用 `ret`(=jalr x0,0(x1))。因此:
//   call = jal/jalr 且 rd==x1(ra);   ret = 0x00008067
// 还有 jal/jalr x0,0(xN) 是尾跳转(不存返回地址), 不算 call 也不算 ret,
// 直接忽略。
static FILE *ftrace_fp = NULL;
static int ftrace_depth = 0;

typedef struct {
    uint32_t start;
    uint32_t size;
    const char *name;
} ftrace_func;
static ftrace_func *ftrace_funcs = NULL;
static size_t ftrace_nfuncs = 0;
static char *ftrace_elfbuf = NULL;  // 整个 ELF 文件缓冲 (name 指针指向其中)

// 手工解析 ELF32: ELF header -> section header table -> .symtab + .strtab
static bool ftrace_load(const char *elf_path) {
    FILE *f = fopen(elf_path, "rb");
    if (!f) {
        fprintf(stderr, "ftrace: cannot open %s\n", elf_path);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    ftrace_elfbuf = (char *)malloc((size_t)sz);
    if (fread(ftrace_elfbuf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return false;
    }
    fclose(f);

    Elf32_Ehdr *eh = (Elf32_Ehdr *)ftrace_elfbuf;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
        eh->e_ident[EI_CLASS] != ELFCLASS32) {
        fprintf(stderr, "ftrace: %s is not a 32-bit ELF\n", elf_path);
        return false;
    }

    Elf32_Shdr *sh = (Elf32_Shdr *)(ftrace_elfbuf + eh->e_shoff);
    Elf32_Shdr *symtab = NULL;
    for (int i = 0; i < eh->e_shnum; i++) {
        if (sh[i].sh_type == SHT_SYMTAB) {
            symtab = &sh[i];
            break;
        }
    }
    if (!symtab) {
        fprintf(stderr, "ftrace: no .symtab\n");
        return false;
    }

    Elf32_Shdr *strtab =
        &sh[symtab->sh_link];  // .symtab 的 sh_link 指向 .strtab
    const char *strs = ftrace_elfbuf + strtab->sh_offset;
    Elf32_Sym *syms = (Elf32_Sym *)(ftrace_elfbuf + symtab->sh_offset);
    int nsym = (int)(symtab->sh_size / sizeof(Elf32_Sym));

    // 只收集 Type=FUNC 且地址非 0 的表项
    ftrace_nfuncs = 0;
    for (int i = 0; i < nsym; i++)
        if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_value != 0)
            ftrace_nfuncs++;

    ftrace_funcs = (ftrace_func *)malloc(ftrace_nfuncs * sizeof(ftrace_func));
    size_t k = 0;
    for (int i = 0; i < nsym; i++) {
        if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC &&
            syms[i].st_value != 0) {
            ftrace_funcs[k].start = syms[i].st_value;
            ftrace_funcs[k].size = syms[i].st_size;
            ftrace_funcs[k].name =
                strs + syms[i].st_name;  // Name 字段是到 .strtab 的偏移
            k++;
        }
    }
    return true;
}

static void ftrace_init(const char *elf_path) {
    if (elf_path && ftrace_load(elf_path)) {
        ftrace_fp = fopen("npc-ftrace.txt", "w");
        if (!ftrace_fp)
            fprintf(stderr, "ftrace: cannot open npc-ftrace.txt\n");
    }
}

static void ftrace_close(void) {
    if (ftrace_fp) {
        fclose(ftrace_fp);
        ftrace_fp = NULL;
    }
}

// 逐项扫描 FUNC, 地址落在 [start, start+size) 内则返回函数名, 否则 "???"
static const char *ftrace_name(uint32_t addr) {
    for (size_t i = 0; i < ftrace_nfuncs; i++)
        if (addr >= ftrace_funcs[i].start &&
            addr < ftrace_funcs[i].start + ftrace_funcs[i].size)
            return ftrace_funcs[i].name;
    return "???";
}

static bool ftrace_is_call(uint32_t inst) {
    return ((inst & 0x7f) == 0x6f || (inst & 0x7f) == 0x67) &&
           ((inst >> 7) & 0x1f) == 1;  // jal/jalr rd=x1(ra)
}
static bool ftrace_is_ret(uint32_t inst) {
    return inst == 0x00008067;  // jalr x0, 0(x1) = ret
}

static void ftrace_call(uint32_t pc, uint32_t target) {
    fprintf(ftrace_fp, "0x%08x: %*scall [%s@0x%08x]\n", pc, ftrace_depth * 2,
            "", ftrace_name(target), target);
    ftrace_depth++;
}
static void ftrace_ret(uint32_t pc) {
    if (ftrace_depth > 0)
        ftrace_depth--;
    fprintf(ftrace_fp, "0x%08x: %*sret  [%s]\n", pc, ftrace_depth * 2, "",
            ftrace_name(pc));
}

// ==================== 对外接口 ====================
void trace_init(const char *elf_path) {
    itrace_init();
    mtrace_init();
    ftrace_init(elf_path);
    disasm_init();
}

void trace_close(void) {
    itrace_close();
    mtrace_close();
    ftrace_close();
    disasm_fini();  // 关 capstone (report_halt 保证在 iring 回放之后调用)
}

// step 在 posedge 后调用: 根据本指令判断 call/ret
void trace_ftrace(uint32_t pc_exec, uint32_t inst_exec, uint32_t next_pc) {
    if (!ftrace_fp)
        return;
    if (ftrace_is_call(inst_exec))
        ftrace_call(pc_exec, next_pc);
    else if (ftrace_is_ret(inst_exec))
        ftrace_ret(pc_exec);
}
