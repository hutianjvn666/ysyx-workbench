#include "npc.h"
#include <string.h>

// Watchpoints: static pool + free/used linked lists.
// Ported from NEMU monitor/sdb/watchpoint.c.

#define NR_WP 32

static WP wp_pool[NR_WP];
static WP *head = NULL, *free_ = NULL;

void init_wp_pool(void) {
    for (int i = 0; i < NR_WP; i++) {
        wp_pool[i].NO = i;
        wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
    }
    head = NULL;
    free_ = wp_pool;
}

WP *new_wp(char *e) {
    if (free_ == NULL) {
        printf("No free watchpoint available.\n");
        return NULL;
    }

    WP *wp = free_;
    free_ = free_->next;

    strncpy(wp->expr, e, sizeof(wp->expr) - 1);
    wp->expr[sizeof(wp->expr) - 1] = '\0';

    // evaluate once as initial old_val; recycle on failure
    bool success;
    wp->old_val = expr(wp->expr, &success);
    if (!success) {
        printf("Failed to evaluate expression \"%s\"\n", e);
        wp->next = free_;
        free_ = wp;
        return NULL;
    }

    wp->next = head;
    head = wp;
    return wp;
}

void free_wp(int NO) {
    WP **p = &head;
    while (*p != NULL) {
        if ((*p)->NO == NO) {
            WP *wp = *p;
            *p = wp->next;
            wp->next = free_;
            free_ = wp;
            printf("Watchpoint %d deleted.\n", NO);
            return;
        }
        p = &(*p)->next;
    }
    printf("Watchpoint %d not found.\n", NO);
}

void wp_display(void) {
    if (head == NULL) {
        printf("No watchpoints.\n");
        return;
    }
    printf("Num\tExpression\t\tValue\n");
    for (WP *wp = head; wp != NULL; wp = wp->next)
        printf("%d\t%s\t\t0x%08x\n", wp->NO, wp->expr, wp->old_val);
}

static int last_hit_no = -1;

int wp_hit_no(void) {
    int no = last_hit_no;
    last_hit_no = -1;  // consume notification
    return no;
}

// Re-evaluate all watchpoints after each instruction.
bool wp_check(void) {
    WP *wp = head;
    bool changed = false;

    while (wp != NULL) {
        bool success;
        uint32_t new_val = expr(wp->expr, &success);
        if (success && new_val != wp->old_val) {
            wp->new_val = new_val;
            last_hit_no = wp->NO;
            changed = true;
        }
        wp = wp->next;
    }

    // update old_val after the full scan
    if (changed) {
        wp = head;
        while (wp != NULL) {
            wp->old_val = wp->new_val;
            wp = wp->next;
        }
    }
    return changed;
}
