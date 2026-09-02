// mem: 测试 lw/lbu/sw/sb
//   a[i] = i*3 用 sw 写, sum += a[i] 用 lw 读 -> 3*(0+1+...+7) = 84
//   p[1] = 0xab 用 sb 写, b = p[1] 用 lbu 读 -> 0xab = 171
int main() {
  volatile int a[8];
  int i;
  for (i = 0; i < 8; i++) a[i] = i * 3;
  int sum = 0;
  for (i = 0; i < 8; i++) sum += a[i];
  volatile unsigned char *p = (unsigned char *)a;
  p[1] = 0xab;
  unsigned char b = p[1];
  return (sum == 84 && b == 0xab) ? 0 : 1;
}
