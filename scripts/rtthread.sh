#!/usr/bin/env bash
# 构建 + 运行 RT-Thread (应见 "Hello RISC-V!" + msh)
source "$(dirname "$0")/env.sh"
cd "$RTT_ROOT/bsp/abstract-machine" || exit 1
make ARCH=riscv32e-npc || exit 1
TRACE_OFF=1 "$NPC_HOME/build/Vtop" build/rtthread-riscv32e-npc.bin
