#include "npc.h"
#include <stdlib.h>
#include <string.h>

// riscv32 register ABI names
static const char *regs[] = {"$0", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
                             "a1", "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
                             "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
#define NR_REG (int)(sizeof(regs) / sizeof(regs[0]))

// info r: print all GPRs + pc
void isa_reg_display(void) {
    for (int i = 0; i < NR_REG; i++) {
        uint32_t v = cpu_gpr(i);
        printf("%-4s\t0x%08x\t%d\n", regs[i], v, v);
    }
    uint32_t pc = cpu_pc();
    printf("pc  \t0x%08x\t%d\n", pc, pc);
}

// Resolve a register name:
//   $pc          -> pc
//   $0-$31    -> numeric index
//   $ra/$a0/$t0 ... -> ABI name ('$' optional)
bool isa_reg_str2val(const char *s, uint32_t *val) {
    const char *name = s;
    if (name[0] == '$')
        name++;  // drop leading '$'

    // pc
    if (strcmp(name, "pc") == 0) {
        *val = cpu_pc();
        return true;
    }

    // numeric $0-$31
    char *end = NULL;
    long idx = strtol(name, &end, 10);
    if (name[0] != '\0' && *end == '\0' && idx >= 0 && idx < NR_REG) {
        *val = cpu_gpr((int)idx);
        return true;
    }

    // ABI names
    for (int i = 0; i < NR_REG; i++) {
        const char *r = regs[i];
        if (r[0] == '$')
            r++;  // "$0" -> "0"
        if (strcmp(name, r) == 0) {
            *val = cpu_gpr(i);
            return true;
        }
    }

    return false;
}
