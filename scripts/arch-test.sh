#!/usr/bin/env bash
# riscv-arch-test E 套件 (37 个)
source "$(dirname "$0")/env.sh"
cd "$WS/riscv-arch-test-am" || exit 1
TRACE_OFF=1 make ARCH=riscv32e-npc TEST_ISA=E run
