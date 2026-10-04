#!/usr/bin/env bash
# DiffTest (NEMU 做 REF): ./difftest.sh <bin>
# 注意: 只比对寄存器, REF 无串口/时钟设备, 纯计算测试才准 (dummy/add 等)
source "$(dirname "$0")/env.sh"
if [ -z "$1" ]; then echo "用法: $0 <bin>  如 ./difftest.sh build/add-riscv32e-npc.bin"; exit 1; fi
REF_SO="$NEMU_HOME/build/riscv32-nemu-interpreter-so"
if [ ! -f "$REF_SO" ]; then echo "先构建 NEMU .so: cd $NEMU_HOME && menuconfig 选 Shared object && make"; exit 1; fi
"$NPC_HOME/build/Vtop" "$1" -d "$REF_SO"
