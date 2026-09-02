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

#include <isa.h>
#include <memory/vaddr.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

enum {
    TK_NOTYPE = 256,
    TK_EQ,
    TK_NEQ,
    TK_AND,
    TK_OR,
    TK_NUM,
    TK_HEX,
    TK_REG
};

static struct rule {
    const char *regex;
    int token_type;
} rules[] = {
    /* Precedence: more specific patterns come first */
    {" +", TK_NOTYPE},            // spaces
    {"0x[0-9a-fA-F]+u", TK_HEX},  // hex numbers with u suffix
    {"0x[0-9a-fA-F]+", TK_HEX},   // hex numbers
    {"[0-9]+u", TK_NUM},          // decimal numbers with u suffix (before plain decimal)
    {"[0-9]+", TK_NUM},           // decimal numbers
    {"\\$[a-zA-Z0-9]+", TK_REG},  // registers ($name)
    {"==", TK_EQ},                // equal
    {"!=", TK_NEQ},               // not equal
    {"&&", TK_AND},               // logical AND
    {"\\|\\|", TK_OR},            // logical OR
    {"\\+", '+'},                 // plus
    {"-", '-'},                   // minus
    {"\\*", '*'},                 // multiply / dereference
    {"/", '/'},                   // divide
    {"\\(", '('},                 // left paren
    {"\\)", ')'},                 // right paren
};

#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
    int i;
    char error_msg[128];
    int ret;

    for (i = 0; i < NR_REGEX; i++) {
        ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
        if (ret != 0) {
            regerror(ret, &re[i], error_msg, 128);
            panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
        }
    }
}

typedef struct token {
    int type;
    char str[32];
} Token;

static Token tokens[256] __attribute__((used)) = {};
static int nr_token __attribute__((used)) = 0;

static bool make_token(char *e) {
    int position = 0;
    int i;
    regmatch_t pmatch;

    nr_token = 0;

    while (e[position] != '\0') {
        /* Try all rules one by one. */
        for (i = 0; i < NR_REGEX; i++) {
            if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
                char *substr_start = e + position;
                int substr_len = pmatch.rm_eo;

                position += substr_len;

                switch (rules[i].token_type) {
                    case TK_NOTYPE:
                        /* Skip whitespace */
                        break;
                    case TK_NUM:
                    case TK_HEX:
                    case TK_REG:
                        /* Record token with string value */
                        tokens[nr_token].type = rules[i].token_type;
                        {
                            int copy_len = substr_len < 31 ? substr_len : 31;
                            memcpy(tokens[nr_token].str, substr_start, copy_len);
                            tokens[nr_token].str[copy_len] = '\0';
                        }
                        nr_token++;
                        break;
                    default:
                        /* Single-char and multi-char operators */
                        tokens[nr_token].type = rules[i].token_type;
                        nr_token++;
                        break;
                }

                break;
            }
        }

        if (i == NR_REGEX) {
            printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
            return false;
        }
    }

    return true;
}

/* Evaluate expression tokens in range [p, q) */
static word_t eval(int p, int q, bool *success) {
    if (p >= q) {
        *success = false;
        return 0;
    }

    /* ---- Strip outer parentheses ---- */
    if (tokens[p].type == '(') {
        int depth = 1;
        int k;
        for (k = p + 1; k < q; k++) {
            if (tokens[k].type == '(')
                depth++;
            else if (tokens[k].type == ')')
                depth--;
            if (depth == 0)
                break;
        }
        if (k == q - 1) {
            word_t ret = eval(p + 1, q - 1, success);
            /* Paren-stripped expression should succeed if inner eval succeeded;
             * success flag already set by inner eval. If inner eval failed, return 0. */
            return ret;
        }
    }

    /* ---- Find dominant binary operator (lowest precedence) at depth 0 ---- */
    int op_pos = -1;
    int paren_depth = 0;
    int op_prec = 999;

    for (int i = p; i < q; i++) {
        if (tokens[i].type == '(') {
            paren_depth++;
        } else if (tokens[i].type == ')') {
            paren_depth--;
        } else if (paren_depth == 0) {
            int prec;
            switch (tokens[i].type) {
                case TK_OR:
                    prec = 1;
                    break;
                case TK_AND:
                    prec = 2;
                    break;
                case TK_EQ:
                case TK_NEQ:
                    prec = 3;
                    break;
                case '+':
                case '-':
                    prec = 4;
                    break;
                case '*':
                case '/':
                    prec = 5;
                    break;
                default:
                    prec = 999;
                    break;
            }

            /* Distinguish unary from binary operators:
             * * and - are unary if they are the first token or follow an operator / '(' */
            if (prec <= 5 && (tokens[i].type == '*' || tokens[i].type == '-')) {
                if (i == p || tokens[i - 1].type == '(' || tokens[i - 1].type == '+' || tokens[i - 1].type == '-' ||
                    tokens[i - 1].type == '*' || tokens[i - 1].type == '/' || tokens[i - 1].type == TK_EQ ||
                    tokens[i - 1].type == TK_NEQ || tokens[i - 1].type == TK_AND || tokens[i - 1].type == TK_OR) {
                    continue; /* This is unary, not binary */
                }
            }

            if (prec < 999 && prec <= op_prec) {
                op_prec = prec;
                op_pos = i;
            }
        }
    }

    if (op_pos != -1) {
        word_t left = eval(p, op_pos, success);
        if (!*success)
            return 0;
        word_t right = eval(op_pos + 1, q, success);
        if (!*success)
            return 0;

        *success = true;
        switch (tokens[op_pos].type) {
            case TK_OR:
                return left || right;
            case TK_AND:
                return left && right;
            case TK_EQ:
                return left == right;
            case TK_NEQ:
                return left != right;
            case '+':
                return left + right;
            case '-':
                return left - right;
            case '*':
                return left * right;
            case '/':
                if (right == 0) {
                    *success = false;
                    return 0;
                }
                return left / right;
            default:
                *success = false;
                return 0;
        }
    }

    /* ---- No binary operator: handle unary and atomic tokens ---- */

    /* Unary minus: -expr */
    if (tokens[p].type == '-') {
        word_t val = eval(p + 1, q, success);
        if (!*success)
            return 0;
        *success = true;
        return ~val + 1; /* two's complement negation, avoids UB */
    }

    /* Dereference: *expr (read 4 bytes from memory) */
    if (tokens[p].type == '*') {
        word_t addr = eval(p + 1, q, success);
        if (!*success)
            return 0;
        *success = true;
        return vaddr_read(addr, 4);
    }

    /* ---- Atomic: single-token expressions ---- */
    if (p + 1 != q) {
        *success = false;
        return 0;
    }

    if (tokens[p].type == TK_NUM) {
        word_t val;
        sscanf(tokens[p].str, "%u", &val);
        *success = true;
        return val;
    }

    if (tokens[p].type == TK_HEX) {
        word_t val;
        sscanf(tokens[p].str, "%x", &val);
        *success = true;
        return val;
    }

    if (tokens[p].type == TK_REG) {
        word_t val = isa_reg_str2val(tokens[p].str, success);
        return val;
    }

    /* Unrecognized token */
    *success = false;
    return 0;
}

word_t expr(char *e, bool *success) {
    if (!make_token(e)) {
        *success = false;
        return 0;
    }

    if (nr_token == 0) {
        *success = false;
        return 0;
    }

    return eval(0, nr_token, success);
}
