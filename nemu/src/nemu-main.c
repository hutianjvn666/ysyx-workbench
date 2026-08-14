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

#include <common.h>

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();
word_t expr(char *e, bool *success);

/* Declarations from other modules (needed for --test-expr) */
void init_rand();
void init_mem();
void init_isa();
void init_regex();
void init_wp_pool();

int main(int argc, char *argv[]) {
#ifdef CONFIG_TARGET_AM
  am_init_monitor();
  engine_start();
  return is_exit_status_bad();
#else
  /* Batch expression test mode */
  if (argc >= 2 && strcmp(argv[1], "--test-expr") == 0) {
    init_rand();
    init_mem();
    init_isa();
    init_regex();
    init_wp_pool();
    char line[65536];
    int total = 0, passed = 0;
    while (fgets(line, sizeof(line), stdin)) {
      unsigned expected;
      char expr_str[65536];
      if (sscanf(line, "%u %65535[^\r\n]", &expected, expr_str) != 2) continue;
      bool success;
      word_t result = expr(expr_str, &success);
      if (success && result == expected) {
        passed++;
      } else {
        printf("FAIL: %s\n      expected %u, got %s%u\n",
               expr_str, expected, success ? "" : "ERR:", result);
      }
      total++;
    }
    printf("\n=== %d / %d passed ===\n", passed, total);
    return (passed == total) ? 0 : 1;
  }

  /* Initialize the monitor. */
  init_monitor(argc, argv);
  engine_start();
  return is_exit_status_bad();
#endif
}
