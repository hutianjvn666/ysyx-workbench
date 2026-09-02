// Quick disassembler to find unimplemented opcodes
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

const char* op_name(uint32_t op) {
    switch(op){
        case 0x37: return "LUI"; case 0x17: return "AUIPC";
        case 0x6F: return "JAL"; case 0x67: return "JALR";
        case 0x63: return "BRANCH"; case 0x03: return "LOAD"; case 0x23: return "STORE";
        case 0x13: return "OP-IMM"; case 0x33: return "OP";
        case 0x0F: return "FENCE"; case 0x73: return "SYSTEM";
        default: return "UNK";
    }
}

const char* sub_name(uint32_t op, uint32_t f3, uint32_t f7, uint32_t instr) {
    static char buf[32];
    if (op == 0x13) {
        const char* n[] = {"ADDI","SLLI","SLTI","SLTIU","XORI","SRLI/SRAI","ORI","ANDI"};
        return n[f3];
    }
    if (op == 0x33) {
        if (f7 == 0x20 && f3 == 0) return "SUB";
        if (f7 == 0x20 && f3 == 5) return "SRA";
        const char* n[] = {"ADD","SLL","SLT","SLTU","XOR","SRL","OR","AND"};
        return n[f3];
    }
    if (op == 0x63) {
        const char* n[] = {"BEQ","BNE","??","??","BLT","BGE","BLTU","BGEU"};
        return n[f3];
    }
    if (op == 0x03) {
        const char* n[] = {"LB","LH","LW","??","LBU","LHU","??","??"};
        return n[f3];
    }
    if (op == 0x23) {
        const char* n[] = {"SB","SH","SW","??","??","??","??","??"};
        return n[f3];
    }
    if (op == 0x73) {
        if (instr == 0x00100073) return "EBREAK";
        if (instr == 0x00000073) return "ECALL";
        return "SYS?";
    }
    return "";
}

#define MAX 4096
typedef struct { char key[40]; int count; uint32_t first_addr; } Entry;
Entry table[MAX];
int n_entries = 0;

void add_entry(const char* key, uint32_t addr) {
    for (int i = 0; i < n_entries; i++) {
        if (strcmp(table[i].key, key) == 0) {
            table[i].count++;
            return;
        }
    }
    strncpy(table[n_entries].key, key, 39);
    table[n_entries].count = 1;
    table[n_entries].first_addr = addr;
    n_entries++;
}

int main(int argc, char** argv) {
    FILE* fp = fopen(argv[1], "r");
    char line[4096];
    fgets(line, sizeof(line), fp); // skip header
    while (fgets(line, sizeof(line), fp)) {
        char* p = strchr(line, ':');
        if (!p) continue;
        uint32_t addr = strtoul(line, NULL, 16);
        p++;
        uint32_t word_idx = 0;
        char* tok = strtok(p, " \t\r\n");
        while (tok) {
            uint32_t instr = (uint32_t)strtoul(tok, NULL, 16);
            uint32_t op = instr & 0x7F;
            uint32_t f3 = (instr >> 12) & 7;
            uint32_t f7 = (instr >> 25) & 0x7F;
            char key[40];
            const char* sub = sub_name(op, f3, f7, instr);
            if (sub[0])
                snprintf(key, sizeof(key), "%s/%s", op_name(op), sub);
            else
                snprintf(key, sizeof(key), "%s", op_name(op));
            add_entry(key, (addr + word_idx) * 4);
            tok = strtok(NULL, " \t\r\n");
            word_idx++;
        }
    }
    fclose(fp);

    // Sort by count descending
    for (int i = 0; i < n_entries-1; i++) {
        for (int j = i+1; j < n_entries; j++) {
            if (table[j].count > table[i].count) {
                Entry t = table[i]; table[i] = table[j]; table[j] = t;
            }
        }
    }
    printf("Instruction frequency:\n");
    for (int i = 0; i < n_entries; i++) {
        printf("  %-25s  %5d   (first at byte 0x%x)\n", table[i].key, table[i].count, table[i].first_addr);
    }
    return 0;
}
