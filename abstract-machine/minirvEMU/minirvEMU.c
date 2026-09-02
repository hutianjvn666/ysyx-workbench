// Copyright [2026] <hutianjvn>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>

#include "dbg.h"
#include <am.h>

#define MAX_CYCLE 2000000
#define MAX_REGISTER 32
#define MAX_RAM (1<<20)
#define MAX_ROM (1<<20)

#define VGA_BASE_ADDR 0x20000000
#define VGA_WIDTH     256
#define VGA_HEIGHT    256
#define VGA_SIZE      (VGA_WIDTH * VGA_HEIGHT * 4)
uint8_t vga_buf[VGA_SIZE] = {0};

uint32_t ram_read32(const uint8_t *ram) {
    return (uint32_t)*ram
        | ((uint32_t)*(ram+1) << 8)
        | ((uint32_t)*(ram+2) << 16)
        | ((uint32_t)*(ram+3) << 24);
}

void ram_write32(uint8_t *ram, const uint32_t value) {
    *ram = value & 0xFF;
    *(ram+1) = (value >> 8) & 0xFF;
    *(ram+2) = (value >> 16) & 0xFF;
    *(ram+3) = (value >> 24) & 0xFF;
}

int32_t sign_extend12(uint32_t imm) {
    if (imm & 0x800)
        return (int32_t)(imm | 0xFFFFF000);
    return (int32_t)imm;
}

int32_t sign_extend20(uint32_t imm) {
    if (imm & 0x80000)
        return (int32_t)(imm | 0xFFF00000);
    return (int32_t)imm;
}

void inst_cycle(uint32_t *reg, uint32_t *pc, uint8_t *ram,
        const uint32_t *rom, int *ebreak_flag) {
    uint32_t instr = rom[(*pc) >> 2];

    uint32_t opcode = instr & 0x7F;
    uint32_t rd     = (instr >> 7) & 0x1F;
    uint32_t rs1    = (instr >> 15) & 0x1F;
    uint32_t rs2    = (instr >> 20) & 0x1F;
    uint32_t funct3 = (instr >> 12) & 0x7;
    uint32_t funct7 = (instr >> 25) & 0x7F;

    uint32_t imm_I = (instr >> 20) & 0xFFF;
    uint32_t imm_S = ((instr >> 25) << 5) | ((instr >>7) & 0x1F);
    uint32_t imm_U = instr & 0xFFFFF000;

    uint32_t next_pc = *pc + 4;
    uint32_t addr = 0;

    switch (opcode) {
        case 0x37:  // 0110111 LUI
            reg[rd] = imm_U;
            break;
        case 0x67:  // 1100111 JALR
            if (funct3 == 0x0) {
                next_pc = (reg[rs1] + sign_extend12(imm_I)) & ~1u;
                reg[rd] = *pc + 4;
            }
            break;
        case 0x03:  // 0000011 LW LBU
            addr = reg[rs1] + sign_extend12(imm_I);
            if (funct3 == 0x2) {
                reg[rd] = ram_read32(ram + addr);
            } else if (funct3 == 0x4) {
                reg[rd] = ram[addr];
            }
            break;
        case 0x23:  // 0100011 SB SW
            addr = reg[rs1] + (int32_t)sign_extend12(imm_S);
            if (addr >= VGA_BASE_ADDR && addr < VGA_BASE_ADDR + VGA_SIZE) {
                uint32_t vga_offset = addr - VGA_BASE_ADDR;
                if (funct3 == 0x2) {
                    if (vga_offset + 3 < VGA_SIZE) {
                        vga_buf[vga_offset]   = reg[rs2] & 0xFF;
                        vga_buf[vga_offset+1] = (reg[rs2] >> 8) & 0xFF;
                        vga_buf[vga_offset+2] = (reg[rs2] >> 16) & 0xFF;
                        vga_buf[vga_offset+3] = (reg[rs2] >> 24) & 0xFF;
                    }
                } else if (funct3 == 0x0) {
                    if (vga_offset < VGA_SIZE) {
                        vga_buf[vga_offset] = reg[rs2] & 0xFF;
                    }
                } else {
                    log_err("Wrong instruction %d (wrong funct3)", funct3);
                }
            } else {
                if (funct3 == 0x2) {
                    ram_write32(ram+addr, reg[rs2]);
                } else if (funct3 == 0x0) {
                    ram[addr] = reg[rs2] & 0xFF;
                } else {
                    log_err("Wrong instruction %d (wrong funct3)", funct3);
                }
            }
            break;
        case 0x13:  // 0010011 ADDI
            if (funct3 == 0x0) {
                reg[rd] = reg[rs1] + sign_extend12(imm_I);
            }
            break;
        case 0x33:  // 0110011 ADD
            if (funct3 == 0x0 && funct7 == 0x0) {
                reg[rd] = reg[rs1] + reg[rs2];
            }
            break;
        case 0x73:  // ebreak
            if (funct3 == 0x0 && instr == 0x00100073) {
                *ebreak_flag = 1;
                return;
            } else {
                log_err("Unsupported system instruction: 0x%08X", instr);
            }
            break;
        default:
            log_err("Wrong instruction %X (wrong opcode)", instr);
    }
    reg[0] = 0;
    *pc = next_pc;
}

int load_image(const char *filename,
                      uint32_t *rom, int max_rom_words,
                      uint8_t *ram, int ram_size_max) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    int rom_words = 0;
    uint8_t buf[4];
    while (rom_words < max_rom_words) {
        size_t read_bytes = fread(buf, 1, 4, fp);
        if (read_bytes == 0) {
            break;
        }
        if (read_bytes != 4) {
            fprintf(stderr, "Unexpected EOF while reading\n");
            fclose(fp);
            return -1;
        }
        rom[rom_words] = (uint32_t)buf[0]
               | ((uint32_t)buf[1] << 8)
               | ((uint32_t)buf[2] << 16)
               | ((uint32_t)buf[3] << 24);
        ram_write32(ram + rom_words * 4, rom[rom_words]);
        rom_words++;
    }
    fclose(fp);
    printf("Loaded memory: %u words\n", rom_words);
    return rom_words;
}

void vga_init() {
    ioe_init();
    printf("VGA display initialized (resolution: %dx%d)\n",
            VGA_WIDTH, VGA_HEIGHT);
}

void vga_display() {
    AM_GPU_FBDRAW_T req = {
        .x = 0, .y = 0,
        .w = VGA_WIDTH, .h = VGA_HEIGHT,
        .pixels = (void *)vga_buf,
        .sync = true
    };
    ioe_write(AM_GPU_FBDRAW, &req);
}

int main(const char *args) {
    if (!args || args[0] == '\0') {
        fprintf(stderr, "Usage: make run ARCH=native mainargs=<image_file>\n");
        return 1;
    }

    vga_init();

    uint32_t reg[MAX_REGISTER] = {0};
    uint32_t pc = 0;
    uint8_t ram[MAX_RAM] = {0};
    uint32_t rom[MAX_ROM] = {0};
    int ebreak_flag = 0;

    int words = load_image(args, rom, MAX_ROM, ram, MAX_RAM);
    if (words < 0) {
        return 1;
    }
    printf("%d instructions are loaded from %s\n", words, args);

    int cycle = 0;
    for (uint32_t last_pc = 0xFFFFFFFF; cycle < MAX_CYCLE; ++cycle) {
        inst_cycle(reg, &pc, ram, rom, &ebreak_flag);

        if (pc == last_pc) {
            printf("Deadloop met, break at PC = 0x%08X\n", pc);
            break;
        }
        if (ebreak_flag) {
            if (reg[10] == 0) {
                printf("HIT GOOD TRAP at PC = 0x%08X, cycle = %d\n",
                        pc, cycle);
            } else {
                printf("HIT BAD TRAP at PC = 0x%08X, cycle = %d, a0 = %d\n",
                        pc, cycle, reg[10]);
            }
            break;
        }

        last_pc = pc;
        if (cycle % 1000 == 0) {
            printf("PC = 0x%08X (cycle: %d)\n", pc, cycle);
        }
    }

    printf("Displaying VGA buffer...\n");
    vga_display();

    while (1) {
        vga_display();
        usleep(10000);
    }

    printf("%d cycles executed. reg[10] = %d\n", cycle, reg[10]);
    return 0;
}
