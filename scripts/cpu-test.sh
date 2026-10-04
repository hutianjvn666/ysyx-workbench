#!/usr/bin/env bash
# 跑单个 cpu-test: ./cpu-test.sh add
source "$(dirname "$0")/env.sh"
if [ -z "$1" ]; then echo "用法: $0 <test名>  如 ./cpu-test.sh add"; exit 1; fi
cd "$WS/am-kernels/tests/cpu-tests" || exit 1
TRACE_OFF=1 make ARCH=riscv32e-npc run ALL=$1
