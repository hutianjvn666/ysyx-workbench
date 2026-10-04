#!/usr/bin/env bash
# 进交互调试器: ./sdb.sh <bin>   (命令: c/q/si N/info r/x N EXPR/p EXPR/w EXPR/d N)
source "$(dirname "$0")/env.sh"
if [ -z "$1" ]; then echo "用法: $0 <bin>"; exit 1; fi
"$NPC_HOME/build/Vtop" "$1" -s
