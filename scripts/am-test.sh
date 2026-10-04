#!/usr/bin/env bash
# 单个 am-test: ./am-test.sh hello
source "$(dirname "$0")/env.sh"
if [ -z "$1" ]; then echo "用法: $0 <test名>  如 ./am-test.sh hello"; exit 1; fi
cd "$WS/am-kernels/tests/am-tests" || exit 1
TRACE_OFF=1 make ARCH=riscv32e-npc mainargs=$1 run
