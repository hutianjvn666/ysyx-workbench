/***************************************************************************************
 * Copyright (c) 2014-2024 Zihao Yu, Nanjing University
 *
 * NEMU is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the Mulan PSL v2 for more details.
 ***************************************************************************************/

#include <common.h>
#include <cpu/decode.h>
#include <elf.h>

// ==================== ftrace (函数调用踪迹) ====================
// 从 ELF 的 .symtab + .strtab 手工解析出「地址 -> 函数名」映射, 追踪调用/返回。
// riscv32 调用约定: 调用用 `jal ra,target`(近距离) 或 `jalr ra`(间接, 目标在寄存器);
// 返回用 `ret`(=jalr x0,0(x1))。因此:
//   call = jal/jalr 且 rd==x1(ra);   ret = 0x00008067
// 还有 jal/jalr x0,0(xN) 是尾跳转(不存返回地址), 不算 call 也不算 ret, 直接忽略。

static FILE *ftrace_fp = NULL;
static int   ftrace_depth = 0;

typedef struct {
  uint32_t start;
  uint32_t size;
  const char *name;
} ftrace_func_t;

static ftrace_func_t *ftrace_funcs = NULL;
static size_t        ftrace_nfuncs = 0;
static char         *ftrace_elfbuf = NULL;  // 整个 ELF 文件缓冲 (name 指针指向其中)

// 手工解析 ELF32: ELF header -> section header table -> .symtab + .strtab
static bool ftrace_load(const char *elf_path) {
  FILE *f = fopen(elf_path, "rb");
  if (f == NULL) { Log("ftrace: cannot open \x27%s\x27", elf_path); return false; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  ftrace_elfbuf = malloc(sz);
  if (fread(ftrace_elfbuf, 1, sz, f) != (size_t)sz) { fclose(f); return false; }
  fclose(f);

  Elf32_Ehdr *eh = (Elf32_Ehdr *)ftrace_elfbuf;
  if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 || eh->e_ident[EI_CLASS] != ELFCLASS32) {
    Log("ftrace: \x27%s\x27 is not a 32-bit ELF", elf_path);
    return false;
  }

  Elf32_Shdr *sh = (Elf32_Shdr *)(ftrace_elfbuf + eh->e_shoff);
  Elf32_Shdr *symtab = NULL;
  for (int i = 0; i < eh->e_shnum; i++) {
    if (sh[i].sh_type == SHT_SYMTAB) { symtab = &sh[i]; break; }
  }
  if (symtab == NULL) { Log("ftrace: no .symtab in \x27%s\x27", elf_path); return false; }

  Elf32_Shdr *strtab = &sh[symtab->sh_link];  // .symtab 的 sh_link 指向 .strtab
  const char *strs = ftrace_elfbuf + strtab->sh_offset;
  Elf32_Sym  *syms = (Elf32_Sym *)(ftrace_elfbuf + symtab->sh_offset);
  int nsym = (int)(symtab->sh_size / sizeof(Elf32_Sym));

  // 只收集 Type=FUNC 且地址非 0 的表项
  ftrace_nfuncs = 0;
  for (int i = 0; i < nsym; i++)
    if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_value != 0)
      ftrace_nfuncs++;

  ftrace_funcs = malloc(ftrace_nfuncs * sizeof(ftrace_func_t));
  size_t k = 0;
  for (int i = 0; i < nsym; i++) {
    if (ELF32_ST_TYPE(syms[i].st_info) == STT_FUNC && syms[i].st_value != 0) {
      ftrace_funcs[k].start = syms[i].st_value;
      ftrace_funcs[k].size  = syms[i].st_size;
      ftrace_funcs[k].name  = strs + syms[i].st_name;  // Name 字段是到 .strtab 的偏移
      k++;
    }
  }
  Log("ftrace: loaded %zu functions from \x27%s\x27", ftrace_nfuncs, elf_path);
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
  return ((inst & 0x7f) == 0x6f || (inst & 0x7f) == 0x67) && ((inst >> 7) & 0x1f) == 1;  // jal/jalr rd=x1(ra)
}

static bool ftrace_is_ret(uint32_t inst) {
  return inst == 0x00008067;  // jalr x0, 0(x1) = ret
}

static void ftrace_call(vaddr_t pc, vaddr_t target) {
  fprintf(ftrace_fp, FMT_WORD ": %*scall [%s@" FMT_WORD "]\n",
          pc, ftrace_depth * 2, "", ftrace_name(target), target);
  ftrace_depth++;
}

static void ftrace_ret(vaddr_t pc) {
  if (ftrace_depth > 0) ftrace_depth--;
  fprintf(ftrace_fp, FMT_WORD ": %*sret  [%s]\n",
          pc, ftrace_depth * 2, "", ftrace_name(pc));
}

void init_ftrace(const char *elf_path) {
  if (elf_path == NULL) return;  // 无镜像 (built-in image), 无符号表可解析
  // NEMU 加载的是 .bin 纯二进制镜像, 符号表在同名 .elf 里; 是 .bin 就直接读 .elf。
  bool ok = false;
  size_t len = strlen(elf_path);
  if (len > 4 && strcmp(elf_path + len - 4, ".bin") == 0) {
    char *alt = malloc(len + 1);
    strcpy(alt, elf_path);
    strcpy(alt + len - 4, ".elf");
    ok = ftrace_load(alt);
    free(alt);
  } else {
    ok = ftrace_load(elf_path);
  }
  if (!ok) return;
  ftrace_fp = fopen("nemu-ftrace.txt", "w");
  if (ftrace_fp == NULL) Log("ftrace: cannot open nemu-ftrace.txt");
}

void ftrace_exec(Decode *s, vaddr_t dnpc) {
  if (ftrace_fp == NULL) return;
  uint32_t inst = s->isa.inst;
  if (ftrace_is_call(inst))     ftrace_call(s->pc, dnpc);
  else if (ftrace_is_ret(inst)) ftrace_ret(s->pc);
}
