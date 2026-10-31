/*
 * k3cc - Minimal C-subset compiler for K-Three balanced ternary architecture.
 *
 * Compiles a subset of C into K-Three assembly (.s).
 *
 * Supported language features:
 *   - Types: int (9-trit balanced ternary word [-9841..+9841])
 *   - Variables: global variables and local variables
 *   - Functions: arguments, return values, recursion (using r0..r4, r6=link, r7=stack)
 *   - Control flow: if / else, while, for, return
 *   - Expressions: +, -, *, <, >, ==, !=, <=, >=, =, variable access, integer literals
 *   - Builtins: print(x), putc(c), halt() via K-Three syscalls
 *
 * Zero external libraries, strict C11.
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TOKENS 16384
#define MAX_VARS 256
#define MAX_FUNCS 64
#define MAX_IDENT 64
#define MAX_ARGS 5 /* r0..r4 */
#define K3_MAX_WORDS 6561 /* total VM memory words (3^8) */

typedef enum {
    TOK_EOF = 0,
    TOK_INT,
    TOK_IDENT,
    TOK_NUM,
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_FOR,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_ASSIGN,     /* = */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
    TOK_SLASH,      /* / */
    TOK_PERCENT,    /* % */
    TOK_EQ,         /* == */
    TOK_NEQ,        /* != */
    TOK_LT,         /* < */
    TOK_GT,         /* > */
    TOK_LE,         /* <= */
    TOK_GE,         /* >= */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACE,     /* { */
    TOK_RBRACE,     /* } */
    TOK_COMMA,      /* , */
    TOK_LBRACKET,   /* [ */
    TOK_RBRACKET,   /* ] */
    TOK_SEMI        /* ; */
} token_type;

typedef struct {
    token_type type;
    char text[MAX_IDENT];
    int64_t val;
    int line;
} token;

static token g_tokens[MAX_TOKENS];
static size_t g_token_count = 0;
static size_t g_token_pos = 0;

static int g_label_counter = 0;
static bool g_uses_divmod = false;

/* Stack of enclosing loops for break/continue. */
#define MAX_LOOP_DEPTH 32
static int g_break_labels[MAX_LOOP_DEPTH];
static int g_continue_labels[MAX_LOOP_DEPTH];
static size_t g_loop_locals[MAX_LOOP_DEPTH];
static int g_loop_depth = 0;

/* Variable Symbol Table */
typedef struct {
    char name[MAX_IDENT];
    bool is_global;
    int index; /* 0 for 1st local, 1 for 2nd local, etc. */
    int size;  /* 1 for a scalar, N for an array of N words */
} var_symbol;

static var_symbol g_locals[MAX_VARS];
static size_t g_local_count = 0; /* number of symbols */
static size_t g_frame_words = 0; /* number of live frame words */

static char g_globals[MAX_VARS][MAX_IDENT];
static int64_t g_global_values[MAX_VARS];
static int g_global_sizes[MAX_VARS];
static size_t g_global_count = 0;

/* Constant array initializers for globals are collected while parsing and
 * emitted in the data section at the end.  A single pool bounded by the VM
 * memory size keeps the compiler allocation-free. */
static int64_t g_init_pool[K3_MAX_WORDS];
static size_t g_init_pool_used = 0;
static size_t g_global_init_start[MAX_VARS];
static int g_global_init_counts[MAX_VARS];

static void loop_push(int break_label, int continue_label)
{
    if (g_loop_depth >= MAX_LOOP_DEPTH) {
        fprintf(stderr, "k3cc: error: loops nested too deeply\n");
        exit(1);
    }
    g_break_labels[g_loop_depth] = break_label;
    g_continue_labels[g_loop_depth] = continue_label;
    g_loop_locals[g_loop_depth] = g_frame_words;
    g_loop_depth++;
}

static void loop_pop(void)
{
    if (g_loop_depth > 0) {
        g_loop_depth--;
    }
}

static void gen_local_addr(FILE *out, int loc)
{
    /* r3 is the frame pointer: it points one word above the first pushed
     * word of this function's frame.  The k-th pushed word of the frame
     * (parameters first, then locals in declaration order) lives at
     * r3 - (k + 1).  Using a frame pointer keeps addresses stable while
     * expression temporaries are pushed on the stack. */
    int offset = -(g_locals[loc].index + 1);
    fprintf(out, "    li r1, #%d\n", offset);
    fprintf(out, "    add r1, r3, r1\n");
}

/* Emit the address of `name[index]` into r1.  The index expression is
 * parsed by the caller into r0.  No bounds checking is performed. */
static void gen_array_addr(FILE *out, const char *name, int loc)
{
    if (loc >= 0) {
        int offset = -(g_locals[loc].index + g_locals[loc].size);
        fprintf(out, "    li r1, #%d\n", offset);
        fprintf(out, "    add r1, r3, r1\n");
    } else {
        fprintf(out, "    li r1, g_%s\n", name);
    }
    fprintf(out, "    add r1, r1, r0\n");
}

/* Lexer */
static void lex(const char *src)
{
    const char *p = src;
    int line = 1;

    g_token_count = 0;
    while (*p && g_token_count < MAX_TOKENS - 1) {
        if (isspace((unsigned char)*p)) {
            if (*p == '\n') line++;
            p++;
            continue;
        }

        /* Line comments // and block comments */
        if (*p == '/' && *(p + 1) == '/') {
            p += 2;
            while (*p && *p != '\n') p++;
            continue;
        }
        if (*p == '/' && *(p + 1) == '*') {
            p += 2;
            while (*p && !(*p == '*' && *(p + 1) == '/')) {
                if (*p == '\n') line++;
                p++;
            }
            if (*p) p += 2;
            continue;
        }

        token *tok = &g_tokens[g_token_count++];
        tok->line = line;
        tok->text[0] = '\0';
        tok->val = 0;

        if (isdigit((unsigned char)*p)) {
            char *end;
            tok->type = TOK_NUM;
            tok->val = strtoll(p, &end, 10);
            p = end;
            continue;
        }

        if (isalpha((unsigned char)*p) || *p == '_') {
            size_t i = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && i < MAX_IDENT - 1) {
                tok->text[i++] = *p++;
            }
            tok->text[i] = '\0';

            if (strcmp(tok->text, "int") == 0) tok->type = TOK_INT;
            else if (strcmp(tok->text, "if") == 0) tok->type = TOK_IF;
            else if (strcmp(tok->text, "else") == 0) tok->type = TOK_ELSE;
            else if (strcmp(tok->text, "while") == 0) tok->type = TOK_WHILE;
            else if (strcmp(tok->text, "for") == 0) tok->type = TOK_FOR;
            else if (strcmp(tok->text, "return") == 0) tok->type = TOK_RETURN;
            else if (strcmp(tok->text, "break") == 0) tok->type = TOK_BREAK;
            else if (strcmp(tok->text, "continue") == 0) tok->type = TOK_CONTINUE;
            else tok->type = TOK_IDENT;
            continue;
        }

        if (*p == '=' && *(p + 1) == '=') { tok->type = TOK_EQ; p += 2; continue; }
        if (*p == '!' && *(p + 1) == '=') { tok->type = TOK_NEQ; p += 2; continue; }
        if (*p == '<' && *(p + 1) == '=') { tok->type = TOK_LE; p += 2; continue; }
        if (*p == '>' && *(p + 1) == '=') { tok->type = TOK_GE; p += 2; continue; }

        switch (*p) {
        case '=': tok->type = TOK_ASSIGN; p++; break;
        case '+': tok->type = TOK_PLUS; p++; break;
        case '-': tok->type = TOK_MINUS; p++; break;
        case '*': tok->type = TOK_STAR; p++; break;
        case '/': tok->type = TOK_SLASH; p++; break;
        case '%': tok->type = TOK_PERCENT; p++; break;
        case '<': tok->type = TOK_LT; p++; break;
        case '>': tok->type = TOK_GT; p++; break;
        case '(': tok->type = TOK_LPAREN; p++; break;
        case ')': tok->type = TOK_RPAREN; p++; break;
        case '{': tok->type = TOK_LBRACE; p++; break;
        case '}': tok->type = TOK_RBRACE; p++; break;
        case ',': tok->type = TOK_COMMA; p++; break;
        case '[': tok->type = TOK_LBRACKET; p++; break;
        case ']': tok->type = TOK_RBRACKET; p++; break;
        case ';': tok->type = TOK_SEMI; p++; break;
        default:
            fprintf(stderr, "k3cc:%d: error: unexpected character '%c'\n", line, *p);
            exit(1);
        }
    }
    g_tokens[g_token_count].type = TOK_EOF;
    g_tokens[g_token_count].line = line;
}

static token *cur_tok(void)
{
    return &g_tokens[g_token_pos];
}

static token *advance_tok(void)
{
    if (g_token_pos < g_token_count) {
        g_token_pos++;
    }
    return &g_tokens[g_token_pos];
}

static bool match_tok(token_type t)
{
    if (cur_tok()->type == t) {
        advance_tok();
        return true;
    }
    return false;
}

static void expect_tok(token_type t, const char *msg)
{
    if (!match_tok(t)) {
        fprintf(stderr, "k3cc:%d: error: expected %s\n", cur_tok()->line, msg);
        exit(1);
    }
}

/* Code Generator Forward Declarations */
static void parse_expression(FILE *out);
static void parse_statement(FILE *out);
static void parse_block(FILE *out);

static int find_local(const char *name)
{
    for (size_t i = 0; i < g_local_count; ++i) {
        if (strcmp(g_locals[i].name, name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_global(const char *name)
{
    for (size_t i = 0; i < g_global_count; ++i) {
        if (strcmp(g_globals[i], name) == 0) return (int)i;
    }
    return -1;
}

static bool is_global(const char *name)
{
    return find_global(name) >= 0;
}

/* Primary Expression: numbers, identifiers, function calls, parens */
static void parse_primary(FILE *out)
{
    token *tok = cur_tok();

    if (match_tok(TOK_MINUS)) {
        parse_primary(out);
        fprintf(out, "    neg r0, r0\n");
    } else if (match_tok(TOK_PLUS)) {
        parse_primary(out);
    } else if (match_tok(TOK_NUM)) {
        fprintf(out, "    li r0, #%lld\n", (long long)tok->val);
    } else if (tok->type == TOK_IDENT) {
        char name[MAX_IDENT];
        strncpy(name, tok->text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        advance_tok();

        /* Function Call */
        if (match_tok(TOK_LPAREN)) {
            int arg_count = 0;
            if (cur_tok()->type != TOK_RPAREN) {
                while (1) {
                    parse_expression(out);
                    /* Push argument onto stack */
                    fprintf(out, "    push r0\n");
                    arg_count++;
                    if (!match_tok(TOK_COMMA)) break;
                }
            }
            expect_tok(TOK_RPAREN, "')' after function arguments");

            /* Builtin helpers */
            if (strcmp(name, "print") == 0) {
                if (arg_count != 1) {
                    fprintf(stderr,
                            "k3cc:%d: error: print() expects 1 argument\n",
                            tok->line);
                    exit(1);
                }
                fprintf(out, "    pop r0\n");
                fprintf(out, "    trap #3\n");      /* SYS_PUTNUM */
                fprintf(out, "    ldi r0, #10\n");  /* '\n' */
                fprintf(out, "    trap #1\n");      /* SYS_PUTC */
                return;
            } else if (strcmp(name, "putc") == 0) {
                if (arg_count != 1) {
                    fprintf(stderr,
                            "k3cc:%d: error: putc() expects 1 argument\n",
                            tok->line);
                    exit(1);
                }
                fprintf(out, "    pop r0\n");
                fprintf(out, "    trap #1\n");      /* SYS_PUTC */
                return;
            } else if (strcmp(name, "halt") == 0) {
                if (arg_count != 0) {
                    fprintf(stderr,
                            "k3cc:%d: error: halt() expects no arguments\n",
                            tok->line);
                    exit(1);
                }
                fprintf(out, "    halt\n");
                return;
            }

            if (arg_count > MAX_ARGS) {
                fprintf(stderr,
                        "k3cc:%d: error: call to '%s' has %d arguments "
                        "(maximum %d)\n",
                        tok->line, name, arg_count, MAX_ARGS);
                exit(1);
            }

            /* Custom function: Pop arguments into registers r0..r4 in reverse */
            for (int a = arg_count - 1; a >= 0; --a) {
                fprintf(out, "    pop r%d\n", a);
            }
            fprintf(out, "    push r6\n");
            fprintf(out, "    push r3\n");
            fprintf(out, "    call fn_%s\n", name);
            fprintf(out, "    pop r3\n");
            fprintf(out, "    pop r6\n");
        } else {
            /* Variable or array element read */
            int loc = find_local(name);
            if (match_tok(TOK_LBRACKET)) {
                int gl = find_global(name);
                if (loc < 0 && gl < 0) {
                    fprintf(stderr,
                            "k3cc:%d: error: undeclared array '%s'\n",
                            tok->line, name);
                    exit(1);
                }
                if (loc >= 0 && g_locals[loc].size <= 1) {
                    fprintf(stderr,
                            "k3cc:%d: error: '%s' is not an array\n",
                            tok->line, name);
                    exit(1);
                }
                if (loc < 0 && g_global_sizes[gl] <= 1) {
                    fprintf(stderr,
                            "k3cc:%d: error: '%s' is not an array\n",
                            tok->line, name);
                    exit(1);
                }
                parse_expression(out);
                expect_tok(TOK_RBRACKET, "']' after array index");
                gen_array_addr(out, name, loc);
                fprintf(out, "    load r0, [r1]\n");
            } else if (loc >= 0) {
                gen_local_addr(out, loc);
                fprintf(out, "    load r0, [r1]\n");
            } else if (is_global(name)) {
                fprintf(out, "    li r1, g_%s\n", name);
                fprintf(out, "    load r0, [r1]\n");
            } else {
                fprintf(stderr, "k3cc:%d: error: undeclared variable '%s'\n", tok->line, name);
                exit(1);
            }
        }
    } else if (match_tok(TOK_LPAREN)) {
        parse_expression(out);
        expect_tok(TOK_RPAREN, "')'");
    } else {
        fprintf(stderr, "k3cc:%d: error: expected primary expression\n", tok->line);
        exit(1);
    }
}

/* Multiplicative: *, /, % */
static void parse_multiplicative(FILE *out)
{
    parse_primary(out);
    while (cur_tok()->type == TOK_STAR || cur_tok()->type == TOK_SLASH ||
           cur_tok()->type == TOK_PERCENT) {
        token_type op = cur_tok()->type;
        advance_tok();
        fprintf(out, "    push r0\n");
        parse_primary(out);
        fprintf(out, "    mov r1, r0\n");
        fprintf(out, "    pop r0\n");
        if (op == TOK_STAR) {
            fprintf(out, "    mul r0, r0, r1\n");
        } else {
            /* r0 = lhs, r1 = rhs; helper returns quotient in r0 and
             * remainder in r1. */
            g_uses_divmod = true;
            fprintf(out, "    push r6\n");
            fprintf(out, "    push r3\n");
            fprintf(out, "    call __k3_divmod\n");
            fprintf(out, "    pop r3\n");
            fprintf(out, "    pop r6\n");
            if (op == TOK_PERCENT) {
                fprintf(out, "    mov r0, r1\n");
            }
        }
    }
}

/* Additive: +, - */
static void parse_additive(FILE *out)
{
    parse_multiplicative(out);
    while (cur_tok()->type == TOK_PLUS || cur_tok()->type == TOK_MINUS) {
        token_type op = cur_tok()->type;
        advance_tok();
        fprintf(out, "    push r0\n");
        parse_multiplicative(out);
        fprintf(out, "    mov r1, r0\n");
        fprintf(out, "    pop r0\n");
        if (op == TOK_PLUS) {
            fprintf(out, "    add r0, r0, r1\n");
        } else {
            fprintf(out, "    sub r0, r0, r1\n");
        }
    }
}

/* Relational: ==, !=, <, >, <=, >= */
static void parse_relational(FILE *out)
{
    parse_additive(out);
    token_type op = cur_tok()->type;
    if (op == TOK_EQ || op == TOK_NEQ || op == TOK_LT || op == TOK_GT ||
        op == TOK_LE || op == TOK_GE) {
        advance_tok();
        fprintf(out, "    push r0\n");
        parse_additive(out);
        fprintf(out, "    mov r1, r0\n"); /* r1 = rhs */
        fprintf(out, "    pop r0\n");      /* r0 = lhs */
        fprintf(out, "    cmp r2, r0, r1\n"); /* r2 = -1 (lt), 0 (eq), 1 (gt) */

        int lbl_true = ++g_label_counter;
        int lbl_done = ++g_label_counter;

        if (op == TOK_EQ) {
            fprintf(out, "    brz r2, .L%d\n", lbl_true);
        } else if (op == TOK_NEQ) {
            fprintf(out, "    brnz r2, .L%d\n", lbl_true);
        } else if (op == TOK_LT) {
            fprintf(out, "    brn r2, .L%d\n", lbl_true);
        } else if (op == TOK_GT) {
            fprintf(out, "    brp r2, .L%d\n", lbl_true);
        } else if (op == TOK_LE) {
            fprintf(out, "    brn r2, .L%d\n", lbl_true);
            fprintf(out, "    brz r2, .L%d\n", lbl_true);
        } else if (op == TOK_GE) {
            fprintf(out, "    brp r2, .L%d\n", lbl_true);
            fprintf(out, "    brz r2, .L%d\n", lbl_true);
        }
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    jmp .L%d\n", lbl_done);
        fprintf(out, ".L%d:\n", lbl_true);
        fprintf(out, "    ldi r0, #1\n");
        fprintf(out, ".L%d:\n", lbl_done);
    }
}

/* Return true when the tokens at the cursor are `IDENT [ ... ] =`, i.e. an
 * assignment to an array element. */
static bool is_array_assign(void)
{
    if (cur_tok()->type != TOK_IDENT ||
        g_tokens[g_token_pos + 1].type != TOK_LBRACKET) {
        return false;
    }
    size_t i = g_token_pos + 2;
    int depth = 1;
    while (g_tokens[i].type != TOK_EOF) {
        if (g_tokens[i].type == TOK_LBRACKET) {
            depth++;
        } else if (g_tokens[i].type == TOK_RBRACKET) {
            depth--;
            if (depth == 0) break;
        }
        i++;
    }
    return g_tokens[i].type == TOK_RBRACKET &&
           g_tokens[i + 1].type == TOK_ASSIGN;
}

/* Assignment and Full Expression */
static void parse_expression(FILE *out)
{
    if (cur_tok()->type == TOK_IDENT && g_tokens[g_token_pos + 1].type == TOK_ASSIGN) {
        char name[MAX_IDENT];
        strncpy(name, cur_tok()->text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        advance_tok(); /* ident */
        advance_tok(); /* = */

        parse_expression(out); /* leaves result in r0 */

        int loc = find_local(name);
        if (loc >= 0) {
            gen_local_addr(out, loc);
            fprintf(out, "    store r0, [r1]\n");
        } else if (is_global(name)) {
            fprintf(out, "    li r1, g_%s\n", name);
            fprintf(out, "    store r0, [r1]\n");
        } else {
            fprintf(stderr, "k3cc: error: assignment to undeclared variable '%s'\n", name);
            exit(1);
        }
        return;
    }
    if (is_array_assign()) {
        char name[MAX_IDENT];
        strncpy(name, cur_tok()->text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        advance_tok(); /* ident */
        advance_tok(); /* [ */

        int loc = find_local(name);
        int gl = find_global(name);
        if (loc < 0 && gl < 0) {
            fprintf(stderr, "k3cc: error: assignment to undeclared array '%s'\n",
                    name);
            exit(1);
        }
        if ((loc >= 0 && g_locals[loc].size <= 1) ||
            (loc < 0 && g_global_sizes[gl] <= 1)) {
            fprintf(stderr, "k3cc: error: '%s' is not an array\n", name);
            exit(1);
        }

        parse_expression(out); /* index in r0 */
        expect_tok(TOK_RBRACKET, "']' after array index");
        gen_array_addr(out, name, loc);
        fprintf(out, "    push r1\n");   /* save element address */
        expect_tok(TOK_ASSIGN, "'=' after array subscript");
        parse_expression(out);           /* value in r0 */
        fprintf(out, "    pop r1\n");
        fprintf(out, "    store r0, [r1]\n");
        return;
    }
    parse_relational(out);
}

/* Statements */
static void parse_statement(FILE *out);

/* Parse an optionally signed decimal constant used in an initializer.  The
 * value must fit the 9-trit word range so that `li` can encode it. */
static int64_t parse_const_int(const char *what)
{
    int64_t sign = 1;
    if (match_tok(TOK_MINUS)) {
        sign = -1;
    } else {
        (void)match_tok(TOK_PLUS);
    }
    token *v = cur_tok();
    if (v->type != TOK_NUM) {
        fprintf(stderr, "k3cc:%d: error: %s must be a constant integer\n",
                v->line, what);
        exit(1);
    }
    expect_tok(TOK_NUM, what);
    int64_t value = sign * v->val;
    if (value < -9841 || value > 9841) {
        fprintf(stderr, "k3cc:%d: error: %s value %lld does not fit in a "
                        "9-trit word [-9841..9841]\n",
                v->line, what, (long long)value);
        exit(1);
    }
    return value;
}

/* Parse `{ v0, v1, ... }` for a local array of `size` words and emit the
 * stores.  Elements after the last supplied value are zero-filled, matching
 * C semantics for partially initialized aggregates. */
static void emit_local_array_init(FILE *out, const char *name, int size)
{
    int loc = find_local(name);
    int base = -(g_locals[loc].index + g_locals[loc].size);

    if (!match_tok(TOK_LBRACE)) {
        fprintf(stderr,
                "k3cc:%d: error: array initializer must be a brace-enclosed "
                "list\n",
                cur_tok()->line);
        exit(1);
    }

    int count = 0;
    if (cur_tok()->type != TOK_RBRACE) {
        while (1) {
            int64_t value = parse_const_int("array initializer element");
            if (count >= size) {
                fprintf(stderr,
                        "k3cc:%d: error: too many initializers for '%s' "
                        "(size %d)\n",
                        cur_tok()->line, name, size);
                exit(1);
            }
            fprintf(out, "    li r0, #%lld\n", (long long)value);
            fprintf(out, "    li r1, #%d\n", base + count);
            fprintf(out, "    add r1, r3, r1\n");
            fprintf(out, "    store r0, [r1]\n");
            count++;
            if (!match_tok(TOK_COMMA)) break;
        }
    }
    expect_tok(TOK_RBRACE, "'}' after array initializer");

    for (int i = count; i < size; ++i) {
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    li r1, #%d\n", base + i);
        fprintf(out, "    add r1, r3, r1\n");
        fprintf(out, "    store r0, [r1]\n");
    }
}

/* Local variable declaration: int name [N] [= expr] ; (the 'int' token is
 * already consumed).  Scalars emit one zero word on the stack; arrays reserve
 * N words.  Symbols are registered so later references resolve through the
 * frame pointer. */
static void parse_declaration(FILE *out)
{
    token *id = cur_tok();
    expect_tok(TOK_IDENT, "identifier in variable declaration");

    if (g_local_count >= MAX_VARS) {
        fprintf(stderr, "k3cc: error: too many local variables\n");
        exit(1);
    }

    int size = 1;
    if (match_tok(TOK_LBRACKET)) {
        token *n = cur_tok();
        if (n->type != TOK_NUM || n->val <= 0 || n->val > K3_MAX_WORDS) {
            fprintf(stderr,
                    "k3cc:%d: error: array size must be a positive constant "
                    "no larger than %d\n",
                    n->line, K3_MAX_WORDS);
            exit(1);
        }
        expect_tok(TOK_NUM, "array size");
        size = (int)n->val;
        expect_tok(TOK_RBRACKET, "']' after array size");
    }

    if (find_local(id->text) >= 0) {
        fprintf(stderr, "k3cc:%d: error: variable '%s' already declared\n",
                id->line, id->text);
        exit(1);
    }

    strncpy(g_locals[g_local_count].name, id->text, MAX_IDENT - 1);
    g_locals[g_local_count].name[MAX_IDENT - 1] = '\0';
    g_locals[g_local_count].is_global = false;
    g_locals[g_local_count].index = (int)g_frame_words;
    g_locals[g_local_count].size = size;
    g_local_count++;
    g_frame_words += (size_t)size;

    if (size == 1) {
        /* Allocate one word on the stack and zero it. */
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    push r0\n");

        if (match_tok(TOK_ASSIGN)) {
            parse_expression(out);
            int loc = find_local(id->text);
            gen_local_addr(out, loc);
            fprintf(out, "    store r0, [r1]\n");
        }
    } else {
        /* Reserve `size` words; the base is the lowest of them. */
        fprintf(out, "    li r1, #%d\n", size);
        fprintf(out, "    sub r7, r7, r1\n");
        if (match_tok(TOK_ASSIGN)) {
            emit_local_array_init(out, id->text, size);
        }
    }
    expect_tok(TOK_SEMI, "';' after variable declaration");
}

/* Pop `count` frame words off the stack. */
static void emit_frame_pop(FILE *out, size_t count)
{
    if (count == 0) {
        return;
    }
    fprintf(out, "    li r1, #%zu\n", count);
    fprintf(out, "    add r7, r7, r1\n");
}

/* Pop the whole frame (parameters + locals) and return. */
static void emit_function_return(FILE *out)
{
    emit_frame_pop(out, g_frame_words);
    fprintf(out, "    ret\n");
}

/* Emit "if (r0 cond) goto .L<label>".  Conditional branches only carry a
 * 4-trit displacement (-40..40), so the long-range case is expressed as an
 * inverted short branch over an absolute JMP (which uses a 9-trit address). */
static void emit_cond_jump(FILE *out, const char *cond, int label)
{
    int skip = ++g_label_counter;
    fprintf(out, "    %s r0, .L%d\n", cond, skip);
    fprintf(out, "    jmp .L%d\n", label);
    fprintf(out, ".L%d:\n", skip);
}

static void parse_statement(FILE *out)
{
    if (match_tok(TOK_IF)) {
        int lbl_else = ++g_label_counter;
        int lbl_end = ++g_label_counter;

        expect_tok(TOK_LPAREN, "'(' after if");
        parse_expression(out);
        expect_tok(TOK_RPAREN, "')' after if condition");

        emit_cond_jump(out, "brnz", lbl_else);
        parse_statement(out);

        if (match_tok(TOK_ELSE)) {
            fprintf(out, "    jmp .L%d\n", lbl_end);
            fprintf(out, ".L%d:\n", lbl_else);
            parse_statement(out);
            fprintf(out, ".L%d:\n", lbl_end);
        } else {
            fprintf(out, ".L%d:\n", lbl_else);
        }
    } else if (match_tok(TOK_WHILE)) {
        int lbl_loop = ++g_label_counter;
        int lbl_end = ++g_label_counter;

        fprintf(out, ".L%d:\n", lbl_loop);
        expect_tok(TOK_LPAREN, "'(' after while");
        parse_expression(out);
        expect_tok(TOK_RPAREN, "')' after while condition");

        emit_cond_jump(out, "brnz", lbl_end);
        loop_push(lbl_end, lbl_loop);
        parse_statement(out);
        loop_pop();
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
    } else if (match_tok(TOK_FOR)) {
        int lbl_loop = ++g_label_counter;
        int lbl_end = ++g_label_counter;

        expect_tok(TOK_LPAREN, "'(' after for");

        /* Initializer: declaration, expression or empty */
        if (cur_tok()->type == TOK_INT) {
            advance_tok();
            parse_declaration(out);
        } else {
            if (cur_tok()->type != TOK_SEMI) {
                parse_expression(out);
            }
            expect_tok(TOK_SEMI, "';' after for initializer");
        }

        fprintf(out, ".L%d:\n", lbl_loop);

        /* Condition: expression or empty (always true) */
        if (cur_tok()->type != TOK_SEMI) {
            parse_expression(out);
        } else {
            fprintf(out, "    ldi r0, #1\n");
        }
        expect_tok(TOK_SEMI, "';' after for condition");

        /* Remember the step tokens so they can be emitted after the body. */
        size_t step_start = g_token_pos;
        int depth = 0;
        while (cur_tok()->type != TOK_EOF) {
            if (cur_tok()->type == TOK_LPAREN) {
                depth++;
            } else if (cur_tok()->type == TOK_RPAREN) {
                if (depth == 0) break;
                depth--;
            }
            advance_tok();
        }
        size_t step_end = g_token_pos;
        expect_tok(TOK_RPAREN, "')' after for step");

        emit_cond_jump(out, "brnz", lbl_end);
        int lbl_cont = ++g_label_counter;
        loop_push(lbl_end, lbl_cont);
        parse_statement(out);
        loop_pop();

        /* Emit the step expression by rewinding to its tokens.  `continue`
         * jumps here so the step still runs. */
        fprintf(out, ".L%d:\n", lbl_cont);
        size_t after_body = g_token_pos;
        g_token_pos = step_start;
        if (step_start != step_end) {
            parse_expression(out);
        }
        g_token_pos = after_body;

        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
    } else if (match_tok(TOK_BREAK)) {
        expect_tok(TOK_SEMI, "';' after break");
        if (g_loop_depth == 0) {
            fprintf(stderr, "k3cc:%d: error: 'break' outside of a loop\n",
                    cur_tok()->line);
            exit(1);
        }
        emit_frame_pop(out, g_frame_words - g_loop_locals[g_loop_depth - 1]);
        fprintf(out, "    jmp .L%d\n", g_break_labels[g_loop_depth - 1]);
    } else if (match_tok(TOK_CONTINUE)) {
        expect_tok(TOK_SEMI, "';' after continue");
        if (g_loop_depth == 0) {
            fprintf(stderr, "k3cc:%d: error: 'continue' outside of a loop\n",
                    cur_tok()->line);
            exit(1);
        }
        emit_frame_pop(out, g_frame_words - g_loop_locals[g_loop_depth - 1]);
        fprintf(out, "    jmp .L%d\n", g_continue_labels[g_loop_depth - 1]);
    } else if (match_tok(TOK_RETURN)) {
        if (cur_tok()->type != TOK_SEMI) {
            parse_expression(out);
        }
        expect_tok(TOK_SEMI, "';' after return");
        emit_function_return(out);
    } else if (match_tok(TOK_INT)) {
        parse_declaration(out);
    } else if (cur_tok()->type == TOK_LBRACE) {
        parse_block(out);
    } else {
        /* Expression statement */
        parse_expression(out);
        expect_tok(TOK_SEMI, "';' after expression");
    }
}

static void parse_block(FILE *out)
{
    size_t saved_locals = g_local_count;
    size_t saved_words = g_frame_words;

    expect_tok(TOK_LBRACE, "'{' to start block");
    while (cur_tok()->type != TOK_RBRACE && cur_tok()->type != TOK_EOF) {
        parse_statement(out);
    }
    expect_tok(TOK_RBRACE, "'}' to end block");

    /* Block scoping: release the locals declared in this block. */
    emit_frame_pop(out, g_frame_words - saved_words);
    g_local_count = saved_locals;
    g_frame_words = saved_words;
}

/* Parse Functions and Globals */
static void parse_program(FILE *out)
{
    /* Entry point jump to main */
    fprintf(out, "; Generated by k3cc (K-Three C Compiler)\n");
    fprintf(out, "    li r7, #6560\n");
    fprintf(out, "    call fn_main\n");
    fprintf(out, "    halt\n\n");

    while (cur_tok()->type != TOK_EOF) {
        expect_tok(TOK_INT, "type 'int'");
        token *id = cur_tok();
        expect_tok(TOK_IDENT, "identifier");

        if (match_tok(TOK_LPAREN)) {
            /* Function declaration */
            char func_name[MAX_IDENT];
            strncpy(func_name, id->text, sizeof(func_name) - 1);
            func_name[sizeof(func_name) - 1] = '\0';

            g_local_count = 0;
            g_frame_words = 0;

            /* Parse parameter list */
            int param_count = 0;
            if (cur_tok()->type != TOK_RPAREN) {
                while (1) {
                    expect_tok(TOK_INT, "type 'int' for parameter");
                    token *pid = cur_tok();
                    expect_tok(TOK_IDENT, "parameter name");

                    strncpy(g_locals[g_local_count].name, pid->text, MAX_IDENT - 1);
                    g_locals[g_local_count].name[MAX_IDENT - 1] = '\0';
                    g_locals[g_local_count].is_global = false;
                    g_locals[g_local_count].index = (int)g_frame_words;
                    g_local_count++;
                    g_frame_words++;
                    param_count++;

                    if (!match_tok(TOK_COMMA)) break;
                }
            }
            expect_tok(TOK_RPAREN, "')' after parameters");

            if (param_count > MAX_ARGS) {
                fprintf(stderr,
                        "k3cc:%d: error: function '%s' has %d parameters "
                        "(maximum %d)\n",
                        id->line, func_name, param_count, MAX_ARGS);
                exit(1);
            }

            fprintf(out, "fn_%s:\n", func_name);

            /* Push parameters from registers r0..r4 to stack */
            for (int p = 0; p < param_count; ++p) {
                fprintf(out, "    push r%d\n", p);
            }

            /* Establish the frame pointer r3 = r7 + param_count, i.e. one
             * word above the first frame word. */
            fprintf(out, "    li r1, #%d\n", param_count);
            fprintf(out, "    add r3, r7, r1\n");

            parse_block(out);

            /* Default return */
            emit_function_return(out);
            fprintf(out, "\n");
        } else {
            /* Global variable: collect, emit data after all code */
            if (g_global_count >= MAX_VARS) {
                fprintf(stderr, "k3cc: error: too many global variables\n");
                exit(1);
            }
            if (find_global(id->text) >= 0) {
                fprintf(stderr,
                        "k3cc:%d: error: global '%s' already declared\n",
                        id->line, id->text);
                exit(1);
            }
            strncpy(g_globals[g_global_count], id->text, MAX_IDENT - 1);
            g_globals[g_global_count][MAX_IDENT - 1] = '\0';

            int size = 1;
            if (match_tok(TOK_LBRACKET)) {
                token *n = cur_tok();
                if (n->type != TOK_NUM || n->val <= 0 ||
                    n->val > K3_MAX_WORDS) {
                    fprintf(stderr,
                            "k3cc:%d: error: array size must be a positive "
                            "constant no larger than %d\n",
                            n->line, K3_MAX_WORDS);
                    exit(1);
                }
                expect_tok(TOK_NUM, "array size");
                size = (int)n->val;
                expect_tok(TOK_RBRACKET, "']' after array size");
            }

            int64_t init_val = 0;
            int init_count = 0;
            size_t init_start = 0;
            if (match_tok(TOK_ASSIGN)) {
                if (size == 1) {
                    init_val = parse_const_int("global initializer");
                } else {
                    if (!match_tok(TOK_LBRACE)) {
                        fprintf(stderr,
                                "k3cc:%d: error: array initializer must be a "
                                "brace-enclosed list\n",
                                cur_tok()->line);
                        exit(1);
                    }
                    init_start = g_init_pool_used;
                    if (cur_tok()->type != TOK_RBRACE) {
                        while (1) {
                            int64_t value =
                                parse_const_int("array initializer element");
                            if (init_count >= size) {
                                fprintf(stderr,
                                        "k3cc:%d: error: too many "
                                        "initializers for '%s' (size %d)\n",
                                        cur_tok()->line, id->text, size);
                                exit(1);
                            }
                            if (g_init_pool_used >= K3_MAX_WORDS) {
                                fprintf(stderr,
                                        "k3cc: error: too many global "
                                        "initializer words\n");
                                exit(1);
                            }
                            g_init_pool[g_init_pool_used++] = value;
                            init_count++;
                            if (!match_tok(TOK_COMMA)) break;
                        }
                    }
                    expect_tok(TOK_RBRACE, "'}' after array initializer");
                }
            }
            g_global_values[g_global_count] = init_val;
            g_global_sizes[g_global_count] = size;
            g_global_init_start[g_global_count] = init_start;
            g_global_init_counts[g_global_count] = init_count;
            g_global_count++;

            expect_tok(TOK_SEMI, "';' after global variable");
        }
    }

    /* Signed division/modulo helper, emitted only when used. */
    if (g_uses_divmod) {
        fprintf(out, "; __k3_divmod: r0 = r0 / r1, r1 = r0 %% r1 (toward zero)\n");
        fprintf(out, "__k3_divmod:\n");
        fprintf(out, "    brnz r1, .Ldiv_go\n");
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    ldi r1, #0\n");
        fprintf(out, "    ret\n");
        fprintf(out, ".Ldiv_go:\n");
        fprintf(out, "    mov r2, r0\n");
        fprintf(out, "    cmpi r2, #0\n");
        fprintf(out, "    push r2\n");
        fprintf(out, "    mov r2, r1\n");
        fprintf(out, "    cmpi r2, #0\n");
        fprintf(out, "    push r2\n");
        fprintf(out, "    brn r0, .Ldiv_nega\n");
        fprintf(out, "    jmp .Ldiv_absa\n");
        fprintf(out, ".Ldiv_nega:\n");
        fprintf(out, "    neg r0, r0\n");
        fprintf(out, ".Ldiv_absa:\n");
        fprintf(out, "    brn r1, .Ldiv_negb\n");
        fprintf(out, "    jmp .Ldiv_absb\n");
        fprintf(out, ".Ldiv_negb:\n");
        fprintf(out, "    neg r1, r1\n");
        fprintf(out, ".Ldiv_absb:\n");
        fprintf(out, "    mov r2, r0\n");
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, ".Ldiv_loop:\n");
        fprintf(out, "    sub r2, r2, r1\n");
        fprintf(out, "    brn r2, .Ldiv_undo\n");
        fprintf(out, "    addi r0, #1\n");
        fprintf(out, "    jmp .Ldiv_loop\n");
        fprintf(out, ".Ldiv_undo:\n");
        fprintf(out, "    add r2, r2, r1\n");
        fprintf(out, "    pop r4\n");
        fprintf(out, "    pop r1\n");
        fprintf(out, "    mul r4, r1, r4\n");
        fprintf(out, "    brn r4, .Ldiv_negq\n");
        fprintf(out, "    jmp .Ldiv_qdone\n");
        fprintf(out, ".Ldiv_negq:\n");
        fprintf(out, "    neg r0, r0\n");
        fprintf(out, ".Ldiv_qdone:\n");
        fprintf(out, "    brn r1, .Ldiv_negrem\n");
        fprintf(out, "    jmp .Ldiv_remdone\n");
        fprintf(out, ".Ldiv_negrem:\n");
        fprintf(out, "    neg r2, r2\n");
        fprintf(out, ".Ldiv_remdone:\n");
        fprintf(out, "    mov r1, r2\n");
        fprintf(out, "    ret\n\n");
    }

    /* Data section for global variables (emitted after all code so that the
     * instruction stream is never interrupted by data words). */
    if (g_global_count > 0) {
        fprintf(out, "; --- global variables ---\n");
        for (size_t g = 0; g < g_global_count; ++g) {
            if (g_global_sizes[g] > 1) {
                int n = g_global_init_counts[g];
                if (n == 0) {
                    fprintf(out, "g_%s: .space %d\n",
                            g_globals[g], g_global_sizes[g]);
                } else {
                    fprintf(out, "g_%s:\n", g_globals[g]);
                    for (int i = 0; i < n; ++i) {
                        fprintf(out, "    .word %lld\n",
                                (long long)g_init_pool[g_global_init_start[g] +
                                                      (size_t)i]);
                    }
                    if (n < g_global_sizes[g]) {
                        fprintf(out, "    .space %d\n",
                                g_global_sizes[g] - n);
                    }
                }
            } else {
                fprintf(out, "g_%s: .word %lld\n",
                        g_globals[g], (long long)g_global_values[g]);
            }
        }
        fprintf(out, "\n");
    }
}

int main(int argc, char **argv)
{
    const char *in_path = NULL;
    const char *out_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (in_path == NULL) {
            in_path = argv[i];
        }
    }

    if (in_path == NULL) {
        fprintf(stderr, "usage: k3cc <source.c> [-o output.s]\n");
        return 1;
    }

    FILE *fin = fopen(in_path, "rb");
    if (!fin) {
        perror(in_path);
        return 1;
    }

    fseek(fin, 0, SEEK_END);
    long sz = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    char *src = (char *)malloc(sz + 1);
    if (!src) {
        fclose(fin);
        return 1;
    }
    if (fread(src, 1, sz, fin) != (size_t)sz) {
        perror("fread");
        free(src);
        fclose(fin);
        return 1;
    }
    src[sz] = '\0';
    fclose(fin);

    lex(src);
    free(src);

    FILE *fout = stdout;
    if (out_path) {
        fout = fopen(out_path, "w");
        if (!fout) {
            perror(out_path);
            return 1;
        }
    }

    parse_program(fout);

    if (out_path) {
        fclose(fout);
    }

    return 0;
}
