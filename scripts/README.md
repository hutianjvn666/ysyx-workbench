# NPC 测试命令小抄

脚本都在本目录 (`~/ysyx-workbench/scripts/`)，直接 `./脚本名` 运行（都自带环境变量，不用手动 export）。

## 常用速查

| 想干什么 | 命令 |
|---|---|
| 编译 NPC | `./build-npc.sh`（改了 csrc/*.cpp 用 `./build-npc.sh clean`）|
| 全部 cpu-tests | `./cpu-tests.sh`（36 个）|
| 单个 cpu-test | `./cpu-test.sh add` |
| riscv-tests | `./riscv-tests.sh`（i 套件 38 个）|
| riscv-arch-test | `./arch-test.sh`（E 套件 37 个）|
| 单个 am-test | `./am-test.sh hello` |
| RT-Thread | `./rtthread.sh`（应见 "Hello RISC-V!" + msh）|
| 交互调试器 | `./sdb.sh <bin>` |
| 直接跑一个 bin | `./npc-run.sh <bin>` |
| DiffTest | `./difftest.sh <bin>` |

## 手动命令（不靠脚本时的完整版）

### cpu-tests
```bash
cd ~/ysyx-workbench/am-kernels/tests/cpu-tests
export AM_HOME=~/ysyx-workbench/abstract-machine NPC_HOME=~/ysyx-workbench/npc
TRACE_OFF=1 make ARCH=riscv32e-npc run
```

### riscv-tests / arch-test
```bash
cd ~/ysyx-workbench/riscv-tests-am && TRACE_OFF=1 make ARCH=riscv32e-npc TEST_ISA=i run
cd ~/ysyx-workbench/riscv-arch-test-am && TRACE_OFF=1 make ARCH=riscv32e-npc TEST_ISA=E run
```

### RT-Thread
```bash
cd ~/Templates/rt-thread-am/bsp/abstract-machine
export AM_HOME=~/ysyx-workbench/abstract-machine NPC_HOME=~/ysyx-workbench/npc RTT_ROOT=~/Templates/rt-thread-am PATH=~/.local/bin:$PATH
make ARCH=riscv32e-npc
TRACE_OFF=1 ~/ysyx-workbench/npc/build/Vtop build/rtthread-riscv32e-npc.bin
```

### DiffTest
```bash
# 先构建 NEMU .so: cd ~/ysyx-workbench/nemu && menuconfig 选 Build target->Shared object && make
cd ~/ysyx-workbench/npc
./build/Vtop <bin> -d ~/ysyx-workbench/nemu/build/riscv32-nemu-interpreter-so
```

### 主频评估 (yosys-sta)
```bash
cd ~/E5/yosys-sta
make sta DESIGN=top PDK=nangate45 RTL_FILES="$(find ~/ysyx-workbench/npc/vsrc -name '*.v')"
```
注意: 当前 RTL 的 pmem 走 DPI-C，会被 yosys 优化成常数，评估主频要先把取指/访存换成 $readmemh 的 ROM/触发器存储器（见 npc-single-cycle-eval 记录）。

## 关键环境变量
- `AM_HOME=~/ysyx-workbench/abstract-machine`
- `NPC_HOME=~/ysyx-workbench/npc`
- `NEMU_HOME=~/ysyx-workbench/nemu`
- `RTT_ROOT=~/Templates/rt-thread-am`
- `PATH` 要加 `~/.local/bin`（scons 装在这）
- wsl.exe 非交互 shell 不加载 `.bashrc`，所以脚本里都显式 `source env.sh`

## 注意事项
- cpu-tests / RT-Thread 的 bin 链接在 `0x80000000`；`npc/tests` 下旧的 logisim 镜像链接在 `0x0`，别用。
- DiffTest 只比对寄存器(GPR+pc)，不比内存/设备；REF 无设备，prime/shuixianhua 等用 MMIO 的会 diverg（预期，不是 bug）。
- 改 `main.cpp` 后要 `./build-npc.sh clean`（顶层 make 只看 RTL 变没变，不重编 csrc）。
