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

#include <isa.h>

vaddr_t isa_raise_intr(word_t NO, vaddr_t epc) {
#ifdef CONFIG_ETRACE
  log_write("etrace: pc = " FMT_WORD ", cause = " FMT_WORD ", epc = " FMT_WORD "\n",
            cpu.pc, NO, epc);
#endif
  cpu.mepc = epc;      // save the PC of the trapped instruction
  cpu.mcause = NO;     // record the cause
  return cpu.mtvec;    // jump to the trap entry (mtvec)
}

word_t isa_query_intr() {
  return INTR_EMPTY;
}
