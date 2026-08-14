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

#define NR_WP 32

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
    int i;
    for (i = 0; i < NR_WP; i++) {
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

    /* Evaluate the expression to get the initial value */
    bool success;
    wp->old_val = expr(wp->expr, &success);
    if (!success) {
        printf("Failed to evaluate expression \"%s\"\n", e);
        /* Return to free list instead of adding to active list */
        wp->next = free_;
        free_ = wp;
        return NULL;
    }

    /* Insert at head of active list */
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
            /* Return to free list */
            wp->next = free_;
            free_ = wp;
            printf("Watchpoint %d deleted.\n", NO);
            return;
        }
        p = &(*p)->next;
    }
    printf("Watchpoint %d not found.\n", NO);
}

void wp_display() {
    if (head == NULL) {
        printf("No watchpoints.\n");
        return;
    }
    printf("Num\tExpression\t\tValue\n");
    WP *wp = head;
    while (wp != NULL) {
        printf("%d\t%s\t\t" FMT_WORD "\n", wp->NO, wp->expr, wp->old_val);
        wp = wp->next;
    }
}

static int last_hit_no = -1;

int wp_hit_no() {
    int no = last_hit_no;
    last_hit_no = -1;  /* consume the notification */
    return no;
}

bool wp_check() {
    WP *wp = head;
    bool changed = false;

    while (wp != NULL) {
        bool success;
        word_t new_val = expr(wp->expr, &success);
        if (success && new_val != wp->old_val) {
            wp->new_val = new_val;
            last_hit_no = wp->NO;
            changed = true;
        }
        wp = wp->next;
    }

    if (changed) {
        /* Update old_val for all watchpoints after the scan,
         * so that simultaneous hits on multiple watchpoints are handled correctly */
        wp = head;
        while (wp != NULL) {
            wp->old_val = wp->new_val;
            wp = wp->next;
        }
    }

    return changed;
}
