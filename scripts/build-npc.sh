#!/usr/bin/env bash
# 重建 NPC (改了 csrc/*.cpp 要 ./build-npc.sh clean)
source "$(dirname "$0")/env.sh"
cd "$NPC_HOME" || exit 1
if [ "$1" = "clean" ]; then make clean; fi
make
