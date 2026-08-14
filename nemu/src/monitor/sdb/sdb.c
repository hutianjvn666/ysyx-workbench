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

#include "sdb.h"
#include <cpu/cpu.h>
#include <isa.h>
#include <memory/vaddr.h>
#include <readline/history.h>
#include <readline/readline.h>

static int is_batch_mode = false;

void init_regex();
void init_wp_pool();

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char *rl_gets() {
    static char *line_read = NULL;

    if (line_read) {
        free(line_read);
        line_read = NULL;
    }

    line_read = readline("(nemu) ");

    if (line_read && *line_read) {
        add_history(line_read);
    }

    return line_read;
}

static int cmd_c(char *args) {
    cpu_exec(-1);
    return 0;
}

static int cmd_q(char *args) {
    nemu_state.state = NEMU_QUIT;
    return -1;
}

static int cmd_help(char *args);

static int cmd_si(char *args) {
    uint64_t n = 1;
    if (args != NULL) {
        char *arg = strtok(NULL, " ");
        if (arg != NULL) {
            int parsed;
            if (sscanf(arg, "%d", &parsed) == 1 && parsed > 0) {
                n = (uint64_t)parsed;
            } else {
                printf("Invalid argument for si: %s. Using 1.\n", arg);
            }
        }
    }
    cpu_exec(n);
    return 0;
}

static int cmd_info(char *args) {
    if (args == NULL) {
        printf("Usage: info r | info w\n");
        return 0;
    }
    char *subcmd = strtok(NULL, " ");
    if (subcmd == NULL) {
        printf("Usage: info r | info w\n");
        return 0;
    }
    if (strcmp(subcmd, "r") == 0) {
        isa_reg_display();
    } else if (strcmp(subcmd, "w") == 0) {
        wp_display();
    } else {
        printf("Unknown info subcommand: %s\n", subcmd);
    }
    return 0;
}

static int cmd_x(char *args) {
    if (args == NULL) {
        printf("Usage: x N EXPR\n");
        return 0;
    }

    /* Extract N */
    char *n_str = strtok(NULL, " ");
    if (n_str == NULL) {
        printf("Usage: x N EXPR\n");
        return 0;
    }

    int n;
    if (sscanf(n_str, "%d", &n) != 1 || n <= 0) {
        printf("Invalid N: %s\n", n_str);
        return 0;
    }

    /* The rest (after N) is the expression.
     * strtok has already placed '\0' after N, so the expression
     * starts right after that null terminator. */
    char *expr_str = n_str + strlen(n_str) + 1;
    while (*expr_str == ' ')
        expr_str++;  // skip leading spaces
    if (*expr_str == '\0') {
        printf("Usage: x N EXPR\n");
        return 0;
    }

    bool success;
    word_t addr = expr(expr_str, &success);
    if (!success) {
        printf("Failed to evaluate expression: %s\n", expr_str);
        return 0;
    }

    int i;
    for (i = 0; i < n; i++) {
        word_t val = vaddr_read(addr + i * 4, 4);
        printf(FMT_WORD ": " FMT_WORD "\n", addr + i * 4, val);
    }

    return 0;
}

static int cmd_p(char *args) {
    if (args == NULL) {
        printf("Usage: p EXPR\n");
        return 0;
    }

    bool success;
    word_t result = expr(args, &success);
    if (!success) {
        printf("Failed to evaluate expression: %s\n", args);
        return 0;
    }

    printf(FMT_WORD "  %d\n", result, result);
    return 0;
}

static int cmd_w(char *args) {
    if (args == NULL) {
        printf("Usage: w EXPR\n");
        return 0;
    }

    WP *wp = new_wp(args);
    if (wp != NULL) {
        printf("Set watchpoint %d: %s\n", wp->NO, wp->expr);
    }
    return 0;
}

static int cmd_d(char *args) {
    if (args == NULL) {
        printf("Usage: d N\n");
        return 0;
    }

    char *n_str = strtok(NULL, " ");
    if (n_str == NULL) {
        printf("Usage: d N\n");
        return 0;
    }

    int n;
    if (sscanf(n_str, "%d", &n) != 1) {
        printf("Invalid watchpoint number: %s\n", n_str);
        return 0;
    }

    free_wp(n);
    return 0;
}

static struct {
    const char *name;
    const char *description;
    int (*handler)(char *);
} cmd_table[] = {
    {"help", "Display information about all supported commands", cmd_help},
    {"c", "Continue the execution of the program", cmd_c},
    {"q", "Exit NEMU", cmd_q},
    {"si", "Single step execution: si [N]", cmd_si},
    {"info", "Print program state: info r | info w", cmd_info},
    {"x", "Examine memory: x N EXPR", cmd_x},
    {"p", "Evaluate expression: p EXPR", cmd_p},
    {"w", "Set watchpoint: w EXPR", cmd_w},
    {"d", "Delete watchpoint: d N", cmd_d},
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
    /* extract the first argument */
    char *arg = strtok(NULL, " ");
    int i;

    if (arg == NULL) {
        /* no argument given */
        for (i = 0; i < NR_CMD; i++) {
            printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        }
    } else {
        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(arg, cmd_table[i].name) == 0) {
                printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
                return 0;
            }
        }
        printf("Unknown command '%s'\n", arg);
    }
    return 0;
}

void sdb_set_batch_mode() {
    is_batch_mode = true;
}

void sdb_mainloop() {
    if (is_batch_mode) {
        cmd_c(NULL);
        return;
    }

    for (char *str; (str = rl_gets()) != NULL;) {
        /* Strip trailing newline (readline includes it in the returned string) */
        size_t len = strlen(str);
        while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
            str[--len] = '\0';
        }

        /* Save original length before strtok modifies the string */
        char *str_end = str + len;

        /* extract the first token as the command */
        char *cmd = strtok(str, " ");
        if (cmd != NULL) {
            /* treat the remaining string as the arguments,
             * which may need further parsing
             */
            char *args = cmd + strlen(cmd) + 1;
            if (args >= str_end) {
                args = NULL;
            }

#ifdef CONFIG_DEVICE
            extern void sdl_clear_event_queue();
            sdl_clear_event_queue();
#endif

            int i;
            for (i = 0; i < NR_CMD; i++) {
                if (strcmp(cmd, cmd_table[i].name) == 0) {
                    if (cmd_table[i].handler(args) < 0) {
                        return;
                    }
                    break;
                }
            }

            if (i == NR_CMD) {
                printf("Unknown command '%s'\n", cmd);
            }
        }

        /* If the command caused a watchpoint hit, notify before next prompt */
        int hit = wp_hit_no();
        if (hit >= 0) {
            printf("Hit watchpoint %d\n", hit);
        }
    }
}

void init_sdb() {
    /* Compile the regular expressions. */
    init_regex();

    /* Initialize the watchpoint pool. */
    init_wp_pool();
}
