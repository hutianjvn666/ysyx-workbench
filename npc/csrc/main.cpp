#include "npc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==================== 主程序 ====================
// NPC 仿真入口: 解析参数 -> 装载镜像 -> 复位 -> 初始化各子系统 -> 跑执行循环 ->
// 收尾。
//
// 各子系统已拆到独立文件 (跨文件接口见 npc.h):
//   pmem.cpp   内存 + 设备模型 (pmem[]、RTC、串口、DPI-C 入口)
//   trace.cpp  踪迹 (mtrace / itrace / iringbuf / capstone / ftrace)
//   cpu.cpp    执行引擎 (step / cpu_exec / report_halt / 寄存器读取)

static void print_help(const char *prog) {
    printf(
        "Usage: %s <image.bin> [halt_addr] [options]\n"
        "\n"
        "Load a raw RISC-V binary (linked at 0x80000000) and run it.\n"
        "\n"
        "Arguments:\n"
        "  <image.bin>          raw program image to load\n"
        "  [halt_addr]          first non-'-' argument: place an ebreak here\n"
        "                       (for tests with a standalone halt symbol)\n"
        "\n"
        "Options:\n"
        "  -h, --help           show this help and exit\n"
        "  -s, --sdb            enter sdb mode (default: batch run)\n"
        "  --elf <path>         load symbol table from <path> for ftrace\n"
        "  -d, --diff <ref.so>  enable DiffTest by dlopen'ing NEMU's <ref.so>\n"
        "\n"
        "Environment:\n"
        "  TRACE_OFF           set (any value) to skip waveform + itrace\n"
        "  MTRACE=1            log data-memory accesses to npc-mtrace.txt\n"
        "  MTRACE_RANGE=lo,hi  limit mtrace to an address range\n",
        prog);
}

static int usage_error(const char *prog, const char *msg) {
    fprintf(stderr, "%s: %s\n", prog, msg);
    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
    return 1;
}

int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        }
    }

    if (argc < 2)
        return usage_error(argv[0], "missing <image.bin>");

    const char *elf_path = NULL;
    const char *halt_addr_str = NULL;
    const char *diff_so = NULL;
    sdb_mode = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--elf") == 0) {
            if (i + 1 >= argc)
                return usage_error(argv[0], "--elf requires a <path>");
            elf_path = argv[++i];
        } else if (strcmp(argv[i], "-d") == 0 ||
                   strcmp(argv[i], "--diff") == 0) {
            if (i + 1 >= argc)
                return usage_error(argv[0], "-d/--diff requires a <ref.so>");
            diff_so = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--sdb") == 0) {
            sdb_mode = true;
        } else if (argv[i][0] == '-') {
            char msg[64];
            snprintf(msg, sizeof(msg), "unknown option '%s'", argv[i]);
            return usage_error(argv[0], msg);
        } else if (halt_addr_str == NULL) {
            halt_addr_str = argv[i];
        }
    }

    trace_off = getenv("TRACE_OFF") != NULL;

    // 从文件读入程序镜像 (raw binary, 链接在 0x80000000)
    size_t nbytes = pmem_load_image(argv[1]);
    printf("loaded %zu bytes from %s\n", nbytes, argv[1]);

    if (halt_addr_str) {
        uint32_t halt_addr = (uint32_t)strtoul(halt_addr_str, NULL, 0);
        place_ebreak(halt_addr);
        printf("place ebreak at halt() = 0x%08x\n", halt_addr);
    }

    init_sim();            // 建 DUT + 波形 + 复位
    trace_init(elf_path);  // itrace + mtrace + ftrace + capstone
    timer_set_boot();
    init_sdb();
    init_difftest(diff_so, pmem_base(), nbytes);

    if (sdb_mode) {
        sdb_mainloop();  // 交互式调试
    } else {
        cpu_exec(UINT64_MAX);  // 批处理: 一直跑到 ebreak
    }

    int ret = report_halt();

    fini_sim();  // 关闭波形、释放 DUT

    return ret;
}
