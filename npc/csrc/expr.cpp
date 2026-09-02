#include "npc.h"
#include <regex.h>
#include <stdlib.h>
#include <string.h>

// Expression evaluation: POSIX-regex lexer + recursive-descent parser.
// Ported from NEMU monitor/sdb/expr.c.
// Grammar (low -> high precedence):
//   ||  logical or
//   &&  logical and
//   == !=
//   + -
//   * /        (* also deref)
//   ( )  unary +/-  deref *  number  register
//
// numbers: decimal 123, hex 0x1f0
// registers: $pc, $0-$31, ABI names ('$' optional)
// deref: *addr reads 4 bytes

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
    {" +", TK_NOTYPE},            // whitespace
    {"0x[0-9a-fA-F]+", TK_HEX},   // hex
    {"[0-9]+", TK_NUM},           // decimal
    {"\\$[a-zA-Z0-9]+", TK_REG},  // register ($name)
    {"==", TK_EQ},
    {"!=", TK_NEQ},
    {"&&", TK_AND},
    {"\\|\\|", TK_OR},
    {"\\+", '+'},
    {"-", '-'},
    {"\\*", '*'},
    {"/", '/'},
    {"\\(", '('},
    {"\\)", ')'},
};
#define NR_REGEX (int)(sizeof(rules) / sizeof(rules[0]))

static regex_t re[NR_REGEX];

void init_regex(void) {
    for (int i = 0; i < NR_REGEX; i++) {
        char error_msg[128];
        int ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
        if (ret != 0) {
            regerror(ret, &re[i], error_msg, sizeof(error_msg));
            fprintf(stderr, "regex compilation failed: %s\n%s\n", error_msg, rules[i].regex);
        }
    }
}

typedef struct token {
    int type;
    char str[32];
} Token;

static Token tokens[256];
static int nr_token = 0;

// Tokenize e into tokens[] / nr_token.
static bool make_token(char *e) {
    int position = 0;
    regmatch_t pmatch;
    nr_token = 0;

    while (e[position] != '\0') {
        int i;
        for (i = 0; i < NR_REGEX; i++) {
            if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
                char *substr_start = e + position;
                int substr_len = pmatch.rm_eo;
                position += substr_len;

                switch (rules[i].token_type) {
                    case TK_NOTYPE:  // skip whitespace
                        break;
                    case TK_NUM:
                    case TK_HEX:
                    case TK_REG: {
                        int copy_len = substr_len < 31 ? substr_len : 31;
                        tokens[nr_token].type = rules[i].token_type;
                        memcpy(tokens[nr_token].str, substr_start, copy_len);
                        tokens[nr_token].str[copy_len] = '\0';
                        nr_token++;
                        break;
                    }
                    default:  // operator
                        tokens[nr_token].type = rules[i].token_type;
                        nr_token++;
                        break;
                }
                break;
            }
        }
        if (i == NR_REGEX) {
            printf("no match at position %d\n%s\n%*s^\n", position, e, position, "");
            return false;
        }
    }
    return true;
}

// Evaluate tokens[p, q).
static uint32_t eval(int p, int q, bool *success) {
    if (p >= q) {
        *success = false;
        return 0;
    }

    // strip outer parens
    if (tokens[p].type == '(') {
        int depth = 1, k;
        for (k = p + 1; k < q; k++) {
            if (tokens[k].type == '(')
                depth++;
            else if (tokens[k].type == ')')
                depth--;
            if (depth == 0)
                break;
        }
        if (k == q - 1)
            return eval(p + 1, q - 1, success);
    }

    // find the lowest-precedence binary operator at depth 0
    int op_pos = -1, paren_depth = 0, op_prec = 999;
    for (int i = p; i < q; i++) {
        if (tokens[i].type == '(')
            paren_depth++;
        else if (tokens[i].type == ')')
            paren_depth--;
        else if (paren_depth == 0) {
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

            // * / - are unary at start or after an operator/'('
            if (prec <= 5 && (tokens[i].type == '*' || tokens[i].type == '-')) {
                if (i == p || tokens[i - 1].type == '(' || tokens[i - 1].type == '+' || tokens[i - 1].type == '-' ||
                    tokens[i - 1].type == '*' || tokens[i - 1].type == '/' || tokens[i - 1].type == TK_EQ ||
                    tokens[i - 1].type == TK_NEQ || tokens[i - 1].type == TK_AND || tokens[i - 1].type == TK_OR) {
                    continue;  // unary
                }
            }

            if (prec < 999 && prec <= op_prec) {
                op_prec = prec;
                op_pos = i;
            }
        }
    }

    // binary op: recurse both sides
    if (op_pos != -1) {
        uint32_t left = eval(p, op_pos, success);
        if (!*success)
            return 0;
        uint32_t right = eval(op_pos + 1, q, success);
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

    // unary minus
    if (tokens[p].type == '-') {
        uint32_t val = eval(p + 1, q, success);
        if (!*success)
            return 0;
        *success = true;
        return ~val + 1;  // two's-complement negate
    }

    // deref: *expr (read 4 bytes)
    if (tokens[p].type == '*') {
        uint32_t addr = eval(p + 1, q, success);
        if (!*success)
            return 0;
        *success = true;
        return sdb_mem_read(addr);
    }

    // atomic token
    if (p + 1 != q) {
        *success = false;
        return 0;
    }

    if (tokens[p].type == TK_NUM) {
        uint32_t val;
        sscanf(tokens[p].str, "%u", &val);
        *success = true;
        return val;
    }
    if (tokens[p].type == TK_HEX) {
        uint32_t val;
        sscanf(tokens[p].str, "%x", &val);
        *success = true;
        return val;
    }
    if (tokens[p].type == TK_REG) {
        uint32_t val;
        if (isa_reg_str2val(tokens[p].str, &val)) {
            *success = true;
            return val;
        }
        *success = false;
        return 0;
    }

    *success = false;
    return 0;
}

uint32_t expr(char *e, bool *success) {
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
