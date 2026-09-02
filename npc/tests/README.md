# minirv 测试程序

在 NPC (D4 minirv) 上运行的测试程序。

## 一键运行

```bash
./run.sh sum   # 求和 1+...+100 = 5050
./run.sh mem   # 测试 lw/lbu/sw/sb
```

`run.sh` 做了三件事: 编译 → 找 halt() 地址 → 在 NPC 上跑。
跑完看最后一行 `halt code (a0)`:

- `a0 = 0` → 程序正确结束 (main 返回 0)
- `a0 != 0` → 程序出错

## 手动三步

```bash
# 1. 用 minirv 工具链编译 (需要 AM_HOME)
make PROG=sum ARCH=minirv-logisim image

# 2. 从符号表找 halt() 地址
riscv64-linux-gnu-nm build/sum-minirv-logisim.elf | grep ' halt$'

# 3. 在 NPC 上跑 (halt 处会被替换成 ebreak)
../build/Vtop build/sum-minirv-logisim.bin 0x<halt地址>
```

## 全指令手测 (9 条指令)

```bash
riscv64-linux-gnu-as -march=rv32e minirv_test.S -o minirv_test.o
riscv64-linux-gnu-objcopy -O binary -j .text minirv_test.o minirv_test.bin
../build/Vtop minirv_test.bin   # 预期 a0 = 110
```

## 文件

- `sum.c` / `mem.c` — 测试程序 C 源码
- `Makefile` — AM 编译入口 (NAME=$(PROG), 用 PROG= 指定程序)
- `minirv_test.S` — 手写汇编, 覆盖全部 9 条指令
- `run.sh` — 一键运行脚本
