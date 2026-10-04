#!/usr/bin/env bash
# 直接跑一个 bin (关 trace 快): ./npc-run.sh <bin>
source "$(dirname "$0")/env.sh"
if [ -z "$1" ]; then echo "用法: $0 <bin>"; exit 1; fi
TRACE_OFF=1 "$NPC_HOME/build/Vtop" "$1"
