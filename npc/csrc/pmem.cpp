#include "Vtop.h"
#include "npc.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

// ==================== 内存 + 设备模型 ====================
// pmem[] 是 DUT 的物理内存; 设备 (串口/RTC) 映射到固定 MMIO 地址。
// 通过 Verilator 的 DPI-C 接口 (pmem_read/ifetch/write) 被 RTL 调用。

#define PMEM_BASE 0x80000000u
#define PMEM_SIZE (128u << 20)

static uint8_t pmem[PMEM_SIZE] = {0};

#define SERIAL_ADDR 0x10000000
#define RTC_ADDR 0x20000000
// 时钟 (RTC): offset 0/4=uptime 低/高, 8~28=年月日时分秒

// ---- 时钟: boot_us 记录仿真启动时刻, uptime = now - boot_us ----
static uint64_t boot_us = 0;
static uint64_t now_us(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ull + tv.tv_usec;
}
static uint64_t uptime_us(void) {
    return now_us() - boot_us;
}
void timer_set_boot(void) {
    boot_us = now_us();
}

static inline uint32_t addr_to_off(uint32_t addr) {
    return addr - PMEM_BASE;
}

// ---- 镜像装载 / 基址 ----
size_t pmem_load_image(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("fopen");
        exit(1);
    }
    size_t nbytes = fread(pmem, 1, PMEM_SIZE, fp);
    fclose(fp);
    return nbytes;
}
void *pmem_base(void) {
    return (void *)pmem;
}

// ---- 越界访问统计 ----
static uint64_t oob_read_cnt = 0;
static uint64_t oob_write_cnt = 0;
static const int OOB_PRINT_MAX = 16;

static void oob_warn(bool is_write, uint32_t addr) {
    uint64_t *cnt = is_write ? &oob_write_cnt : &oob_read_cnt;
    (*cnt)++;
    if (*cnt <= OOB_PRINT_MAX) {
        fprintf(stderr,
                "[pmem] out-of-bounds %s at 0x%08x, pmem = [0x%08x, 0x%08x), "
                "pc = 0x%08x\n",
                is_write ? "WRITE" : "READ", addr, PMEM_BASE,
                PMEM_BASE + PMEM_SIZE, top ? (uint32_t)top->pc : 0u);
    }
}

void pmem_print_oob_stats(void) {
    if (oob_read_cnt || oob_write_cnt)
        printf("  [pmem] out-of-bounds accesses: %llu read, %llu write\n",
               (unsigned long long)oob_read_cnt,
               (unsigned long long)oob_write_cnt);
}

// sdb / expr 用: 读 4 字节, 越界返回 0
uint32_t sdb_mem_read(uint32_t addr) {
    if (addr < PMEM_BASE)
        return 0;  // 可能是MMIO设备
    uint32_t base = addr_to_off(addr);
    if (base + 3 >= PMEM_SIZE) {
        oob_warn(false, addr);
        return 0;
    }
    return (uint32_t)pmem[base + 0] | ((uint32_t)pmem[base + 1] << 8) |
           ((uint32_t)pmem[base + 2] << 16) | ((uint32_t)pmem[base + 3] << 24);
}

// ---- RTC ----
// offset 0/4 = uptime(us, 低/高 32 位), 8~28 = 年月日时分秒 (GMT)
static int rtc_read(uint32_t offset) {
    static uint64_t rtc_latch = 0;
    if (offset == 4) {
        rtc_latch = uptime_us();
        return (int32_t)(rtc_latch >> 32);
    }
    if (offset == 0) {
        return (int32_t)rtc_latch;
    }
    time_t now = time(NULL);
    struct tm *t = gmtime(&now);
    switch (offset) {
        case 8:
            return t->tm_year + 1900;
        case 12:
            return t->tm_mon + 1;
        case 16:
            return t->tm_mday;
        case 20:
            return t->tm_hour;
        case 24:
            return t->tm_min;
        case 28:
            return t->tm_sec;
        default:
            return 0;
    }
}

// ---- DPI-C 入口 ----
// lw/lbu, 记 mtrace
extern "C" int pmem_read(int raddr) {
    uint32_t addr = (uint32_t)raddr;
    if (addr >= RTC_ADDR && addr < RTC_ADDR + 32)
        return rtc_read(addr - RTC_ADDR);
    uint32_t data = sdb_mem_read(addr);
    mtrace_log('r', addr, data, 0);
    return (int32_t)data;
}

// sw/sb, 按写掩码 wmask 写入 addr 处连续 4 字节, 记 mtrace
extern "C" void pmem_write(int waddr, int wdata, char wmask) {
    uint32_t addr = (uint32_t)waddr;
    if (addr == SERIAL_ADDR) {  // 串口: 输出一个字符
        putchar((unsigned char)(wdata & 0xff));
        fflush(stdout);
        return;
    }
    mtrace_log('w', addr, (uint32_t)wdata, wmask);
    if (addr < PMEM_BASE)
        return;
    uint32_t base = addr_to_off(addr);
    if (base + 3 >= PMEM_SIZE) {
        oob_warn(true, addr);
        return;
    }
    for (int i = 0; i < 4; i++) {
        if (wmask & (1 << i))
            pmem[base + i] = (uint8_t)((wdata >> (i * 8)) & 0xff);
    }
}

extern "C" int pmem_ifetch(int raddr) {
    return (int32_t)sdb_mem_read((uint32_t)raddr);
}

// 在 addr 处写一条 ebreak (main 用, 包装 pmem_write)
void place_ebreak(uint32_t addr) {
    pmem_write((int)addr, 0x00100073, (char)0xf);  // ebreak
}

// DPI-C: NPC 执行 ebreak 指令时被调用, 置 npc_state = NPC_END 通知仿真环境结束。
// 在此捕获停机现场 (pc / 返回值 a0), report_halt 直接使用, 不再读 DUT 内部信号。
extern "C" void ebreak() {
    set_npc_state(NPC_END, (uint32_t)top->pc, (int)top->a0);
}
