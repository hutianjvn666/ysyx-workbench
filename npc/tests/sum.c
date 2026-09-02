// sum: 1 + 2 + ... + 100 = 5050
int main() {
  int i = 1;
  volatile int sum = 0;
  while (i <= 100) {
    sum += i;
    i++;
  }
  return (sum == 5050) ? 0 : 1;
}
