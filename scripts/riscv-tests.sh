#!/usr/bin/env bash
# riscv-tests i 套件 (38 个)
source "$(dirname "$0")/env.sh"
cd "$WS/riscv-tests-am" || exit 1
TRACE_OFF=1 make ARCH=riscv32e-npc TEST_ISA=i run
