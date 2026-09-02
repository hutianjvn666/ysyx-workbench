#include <am.h>
#include <klib.h>
#include <klib-macros.h>
#include <stdarg.h>

#if !defined(__ISA_NATIVE__) || defined(__NATIVE_USE_KLIB__)

// 整数转十进制,写入 buf,返回写入的字符数(会处理负数)
static int write_dec(char *buf, int x) {
  int n = 0;
  unsigned int u;
  if (x < 0) {
    buf[n++] = '-';
    u = (unsigned int)(-(x + 1)) + 1;   // 这样写能正确处理 INT_MIN
  } else {
    u = (unsigned int)x;
  }
  char tmp[16];
  int i = 0;
  do {
    tmp[i++] = '0' + (u % 10);
    u /= 10;
  } while (u != 0);
  while (i > 0) buf[n++] = tmp[--i];
  return n;
}

// 无符号整数转十六进制,写入 buf,返回写入的字符数
static int write_hex(char *buf, unsigned int x) {
  static const char digits[] = "0123456789abcdef";
  char tmp[16];
  int i = 0;
  do {
    tmp[i++] = digits[x & 0xf];
    x >>= 4;
  } while (x != 0);
  int n = 0;
  while (i > 0) buf[n++] = tmp[--i];
  return n;
}

// 把长度为 len 的字符串 s 按宽度 width 右对齐写到 out(不够则用 pad 填充), 返回新的 count
static size_t emit(char *out, size_t n, size_t count,
                   const char *s, int len, int width, char pad) {
  int padn = width - len;
  while (padn-- > 0) {                          // 先写填充
    if (count < n) out[count] = pad;
    count++;
  }
  for (int i = 0; i < len; i++) {               // 再写内容
    if (count < n) out[count] = s[i];
    count++;
  }
  return count;
}

int vsnprintf(char *out, size_t n, const char *fmt, va_list ap) {
  size_t count = 0;   // 已经写出的字符数(不含结尾 '\0')

  for (size_t i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] != '%') {                       // 普通字符
      if (count < n) out[count] = fmt[i];
      count++;
      continue;
    }

    i++;                                        // 解析 '%' 后面的 flag/width
    int zero = 0;
    int width = 0;
    if (fmt[i] == '0') { zero = 1; i++; }
    while (fmt[i] >= '0' && fmt[i] <= '9') { width = width * 10 + (fmt[i] - '0'); i++; }

    switch (fmt[i]) {
      case '%':                                 // "%%" -> 输出一个 '%'
        if (count < n) out[count] = '%';
        count++;
        break;
      case 's': {                               // "%s" (width 右对齐, 补空格)
        const char *s = va_arg(ap, const char *);
        int len = 0;
        while (s[len]) len++;
        count = emit(out, n, count, s, len, width, ' ');
        break;
      }
      case 'd': {                               // "%d" (width 右对齐, 补空格或零)
        int x = va_arg(ap, int);
        char tmp[16];
        int len = write_dec(tmp, x);
        if (zero && len > 0 && tmp[0] == '-') { // 负号单独输出, 数字部分再补零
          if (count < n) out[count] = '-';
          count++;
          count = emit(out, n, count, tmp + 1, len - 1, width - 1, '0');
        } else {
          count = emit(out, n, count, tmp, len, width, zero ? '0' : ' ');
        }
        break;
      }
      case 'c': {                               // "%c"
        char c = (char)va_arg(ap, int);
        count = emit(out, n, count, &c, 1, width, ' ');
        break;
      }
      case 'x': {                               // "%x" (width 右对齐, 补空格或零)
        unsigned int x = va_arg(ap, unsigned int);
        char tmp[16];
        int len = write_hex(tmp, x);
        count = emit(out, n, count, tmp, len, width, zero ? '0' : ' ');
        break;
      }
      case 'p': {                               // "%p" -> "0x" + 十六进制
        void *p = va_arg(ap, void *);
        if (count < n) out[count] = '0';
        count++;
        if (count < n) out[count] = 'x';
        count++;
        char tmp[16];
        int len = write_hex(tmp, (unsigned int)(uintptr_t)p);
        for (int j = 0; j < len; j++) {
          if (count < n) out[count] = tmp[j];
          count++;
        }
        break;
      }
      default:                                  // 不认识的格式符,原样输出
        if (count < n) out[count] = '%';
        count++;
        if (count < n) out[count] = fmt[i];
        count++;
        break;
    }
  }

  if (n > 0) {                                  // 写结尾 '\0'
    if (count < n) out[count] = '\0';
    else out[n - 1] = '\0';
  }
  return (int)count;
}

int vsprintf(char *out, const char *fmt, va_list ap) {
  return vsnprintf(out, (size_t)-1, fmt, ap);   // 不限制长度
}

int sprintf(char *out, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int len = vsprintf(out, fmt, ap);
  va_end(ap);
  return len;
}

int snprintf(char *out, size_t n, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int len = vsnprintf(out, n, fmt, ap);
  va_end(ap);
  return len;
}

int printf(const char *fmt, ...) {
  char buf[1024];                               // 先格式化到缓冲区
  va_list ap;
  va_start(ap, fmt);
  int len = vsprintf(buf, fmt, ap);
  va_end(ap);
  for (int i = 0; i < len; i++) putch(buf[i]);  // 再逐字符输出到控制台
  return len;
}

#endif
