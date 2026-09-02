#include <am.h>
#include <riscv/riscv.h>

#define RTC_ADDR 0x20000000   // 时钟地址 (与后续 SoC 一致)

void __am_timer_init() {
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  uint32_t hi = inl(RTC_ADDR + 4);   // 先读高字(定格)再读低字
  uint32_t lo = inl(RTC_ADDR);
  uptime->us = ((uint64_t)hi << 32) | lo;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->year   = inl(RTC_ADDR + 8);
  rtc->month  = inl(RTC_ADDR + 12);
  rtc->day    = inl(RTC_ADDR + 16);
  rtc->hour   = inl(RTC_ADDR + 20);
  rtc->minute = inl(RTC_ADDR + 24);
  rtc->second = inl(RTC_ADDR + 28);
}
