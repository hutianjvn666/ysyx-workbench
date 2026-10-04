#!/usr/bin/env bash
# 跑全部 cpu-tests (36 个)
source "$(dirname "$0")/env.sh"
cd "$WS/am-kernels/tests/cpu-tests" || exit 1
TRACE_OFF=1 make ARCH=riscv32e-npc run
