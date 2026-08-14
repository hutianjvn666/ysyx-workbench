/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

// this should be enough
static char buf[65536] = {};
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";

static uint32_t choose(uint32_t n) {
  return rand() % n;
}

static void gen_num() {
  char tmp[32];
  unsigned val;
  // occasionally generate boundary values
  switch (choose(10)) {
    case 0: val = 0; break;
    case 1: val = 1; break;
    case 2: val = 0xffffffff; break;
    default: val = rand(); break;
  }
  sprintf(tmp, "%uu", val);  // u suffix → GCC uses 32-bit unsigned, matching NEMU
  strcat(buf, tmp);
}

static void gen_rand_expr() {
  static int depth = 0;
  if (depth > 10 || strlen(buf) > 500) { gen_num(); return; }

  depth++;
  switch (choose(10)) {
    case 0: case 1:
      /* number — 20% */
      if (choose(4) == 0) strcat(buf, " ");
      gen_num();
      if (choose(4) == 0) strcat(buf, " ");
      break;
    case 2: case 3:
      /* ( expr ) — 20% */
      if (choose(4) == 0) strcat(buf, " ");
      strcat(buf, "(");
      if (choose(3) == 0) strcat(buf, " ");
      gen_rand_expr();
      if (choose(3) == 0) strcat(buf, " ");
      strcat(buf, ")");
      if (choose(4) == 0) strcat(buf, " ");
      break;
    default: {
      /* expr op expr */
      if (choose(4) == 0) strcat(buf, " ");
      gen_rand_expr();
      if (choose(4) == 0) strcat(buf, " ");

      int op = choose(4);
      char op_str[4] = {};
      switch (op) {
        case 0: strcpy(op_str, "+"); break;
        case 1: strcpy(op_str, "-"); break;
        case 2: strcpy(op_str, "*"); break;
        case 3: strcpy(op_str, "/"); break;
      }
      strcat(buf, op_str);

      if (op == 3) {
        /* division: ensure RHS is a non-zero literal */
        char tmp[32];
        sprintf(tmp, " %uu ", (unsigned)(rand() % 9999 + 1));
        strcat(buf, tmp);
      } else {
        if (choose(4) == 0) strcat(buf, " ");
        gen_rand_expr();
      }
      if (choose(4) == 0) strcat(buf, " ");
      break;
    }
  }
  depth--;
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  for (i = 0; i < loop; i ++) {
    buf[0] = '\0';
    gen_rand_expr();

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    unsigned result;
    ret = fscanf(fp, "%u", &result);
    pclose(fp);

    printf("%u %s\n", result, buf);
  }
  return 0;
}
