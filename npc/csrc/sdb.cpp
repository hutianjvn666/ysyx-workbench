#include "npc.h"
#include <stdlib.h>
#include <string.h>

// Simple debugger (sdb) command loop. Ported from NEMU monitor/sdb/sdb.c,
// using fgets instead of readline.

// Read one line with prompt; NULL on EOF.
static char *rl_gets(void) {
    static char buf[512];
    printf("(npc) ");
    fflush(stdout);
    if (fgets(buf, sizeof(buf), stdin) == NULL)
        return NULL;
    return buf;
}

static int cmd_c(char *args) {
    cpu_exec(UINT64_MAX);  // run until ebreak / watchpoint / timeout
    return 0;
}

static int cmd_q(char *args) {
    npc_state = NPC_QUIT;
    return -1;
}

static int cmd_help(char *args);

static int cmd_si(char *args) {
    uint64_t n = 1;
    if (args != NULL) {
        char *arg = strtok(NULL, " ");
        if (arg != NULL) {
            int parsed;
            if (sscanf(arg, "%d", &parsed) == 1 && parsed > 0)
                n = (uint64_t)parsed;
            else
                printf("Invalid argument for si: %s. Using 1.\n", arg);
        }
    }
    cpu_exec(n);
    return 0;
}

static int cmd_info(char *args) {
    char *subcmd = strtok(NULL, " ");
    if (subcmd == NULL) {
        printf("Usage: info r | info w\n");
        return 0;
    }
    if (strcmp(subcmd, "r") == 0)
        isa_reg_display();
    else if (strcmp(subcmd, "w") == 0)
        wp_display();
    else
        printf("Unknown info subcommand: %s\n", subcmd);
    return 0;
}

static int cmd_x(char *args) {
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

    // expression follows N
    char *expr_str = n_str + strlen(n_str) + 1;
    while (*expr_str == ' ')
        expr_str++;
    if (*expr_str == '\0') {
        printf("Usage: x N EXPR\n");
        return 0;
    }

    bool success;
    uint32_t addr = expr(expr_str, &success);
    if (!success) {
        printf("Failed to evaluate expression: %s\n", expr_str);
        return 0;
    }

    for (int i = 0; i < n; i++) {
        uint32_t val = sdb_mem_read(addr + i * 4);
        printf("0x%08x: 0x%08x\n", addr + i * 4, val);
    }
    return 0;
}

static int cmd_p(char *args) {
    if (args == NULL) {
        printf("Usage: p EXPR\n");
        return 0;
    }

    bool success;
    uint32_t result = expr(args, &success);
    if (!success) {
        printf("Failed to evaluate expression: %s\n", args);
        return 0;
    }

    printf("0x%08x  %d\n", result, result);
    return 0;
}

static int cmd_w(char *args) {
    if (args == NULL) {
        printf("Usage: w EXPR\n");
        return 0;
    }

    WP *wp = new_wp(args);
    if (wp != NULL)
        printf("Set watchpoint %d: %s\n", wp->NO, wp->expr);
    return 0;
}

static int cmd_d(char *args) {
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
    {"q", "Exit NPC", cmd_q},
    {"si", "Single step execution: si [N]", cmd_si},
    {"info", "Print program state: info r | info w", cmd_info},
    {"x", "Examine memory: x N EXPR", cmd_x},
    {"p", "Evaluate expression: p EXPR", cmd_p},
    {"w", "Set watchpoint: w EXPR", cmd_w},
    {"d", "Delete watchpoint: d N", cmd_d},
};
#define NR_CMD (int)(sizeof(cmd_table) / sizeof(cmd_table[0]))

static int cmd_help(char *args) {
    char *arg = strtok(NULL, " ");
    if (arg == NULL) {
        for (int i = 0; i < NR_CMD; i++)
            printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    } else {
        for (int i = 0; i < NR_CMD; i++) {
            if (strcmp(arg, cmd_table[i].name) == 0) {
                printf("%s - %s\n", cmd_table[i].name,
                       cmd_table[i].description);
                return 0;
            }
        }
        printf("Unknown command '%s'\n", arg);
    }
    return 0;
}

void sdb_mainloop(void) {
    while (npc_state != NPC_END && npc_state != NPC_TIMEOUT &&
           npc_state != NPC_QUIT) {
        char *str = rl_gets();
        if (str == NULL)
            break;  // EOF

        // strip trailing newline
        size_t len = strlen(str);
        while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r'))
            str[--len] = '\0';
        char *str_end = str + len;

        // first token = command
        char *cmd = strtok(str, " ");
        if (cmd == NULL)
            continue;

        // rest is args
        char *args = cmd + strlen(cmd) + 1;
        if (args >= str_end)
            args = NULL;

        int i;
        for (i = 0; i < NR_CMD; i++) {
            if (strcmp(cmd, cmd_table[i].name) == 0) {
                if (cmd_table[i].handler(args) < 0)
                    return;  // q quits
                break;
            }
        }
        if (i == NR_CMD)
            printf("Unknown command '%s'\n", cmd);

        // report watchpoint hit
        int hit = wp_hit_no();
        if (hit >= 0)
            printf("Hit watchpoint %d\n", hit);
    }
}

void init_regex(void);    // expr.cpp
void init_wp_pool(void);  // watchpoint.cpp

void init_sdb(void) {
    init_regex();
    init_wp_pool();
}
