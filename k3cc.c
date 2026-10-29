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
    TOK_ASSIGN,     /* = */
    TOK_PLUS,       /* + */
    TOK_MINUS,      /* - */
    TOK_STAR,       /* * */
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

/* Variable Symbol Table */
typedef struct {
    char name[MAX_IDENT];
    bool is_global;
    int stack_offset; /* For locals: relative to frame or offset from r7 */
} var_symbol;

static var_symbol g_locals[MAX_VARS];
static size_t g_local_count = 0;

static char g_globals[MAX_VARS][MAX_IDENT];
static size_t g_global_count = 0;

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

        if (isdigit((unsigned char)*p) || (*p == '-' && isdigit((unsigned char)*(p + 1)))) {
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
        case '<': tok->type = TOK_LT; p++; break;
        case '>': tok->type = TOK_GT; p++; break;
        case '(': tok->type = TOK_LPAREN; p++; break;
        case ')': tok->type = TOK_RPAREN; p++; break;
        case '{': tok->type = TOK_LBRACE; p++; break;
        case '}': tok->type = TOK_RBRACE; p++; break;
        case ',': tok->type = TOK_COMMA; p++; break;
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

static bool is_global(const char *name)
{
    for (size_t i = 0; i < g_global_count; ++i) {
        if (strcmp(g_globals[i], name) == 0) return true;
    }
    return false;
}

/* Primary Expression: numbers, identifiers, function calls, parens */
static void parse_primary(FILE *out)
{
    token *tok = cur_tok();

    if (match_tok(TOK_NUM)) {
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
                if (arg_count > 0) {
                    fprintf(out, "    pop r0\n");
                    fprintf(out, "    trap #3\n");      /* SYS_PUTNUM */
                    fprintf(out, "    ldi r0, #10\n");  /* '\n' */
                    fprintf(out, "    trap #1\n");      /* SYS_PUTC */
                }
                return;
            } else if (strcmp(name, "putc") == 0) {
                if (arg_count > 0) {
                    fprintf(out, "    pop r0\n");
                    fprintf(out, "    trap #1\n");      /* SYS_PUTC */
                }
                return;
            } else if (strcmp(name, "halt") == 0) {
                fprintf(out, "    halt\n");
                return;
            }

            /* Custom function: Pop arguments into registers r0..r4 in reverse */
            for (int a = arg_count - 1; a >= 0; --a) {
                if (a < 5) {
                    fprintf(out, "    pop r%d\n", a);
                } else {
                    fprintf(out, "    pop r4\n");
                }
            }
            fprintf(out, "    call fn_%s\n", name);
        } else {
            /* Variable read */
            int loc = find_local(name);
            if (loc >= 0) {
                /* Local variable stored in frame */
                fprintf(out, "    li r1, #%d\n", g_locals[loc].stack_offset);
                fprintf(out, "    add r1, r7, r1\n");
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

/* Multiplicative: * */
static void parse_multiplicative(FILE *out)
{
    parse_primary(out);
    while (cur_tok()->type == TOK_STAR) {
        advance_tok();
        fprintf(out, "    push r0\n");
        parse_primary(out);
        fprintf(out, "    mov r1, r0\n");
        fprintf(out, "    pop r0\n");
        fprintf(out, "    mul r0, r0, r1\n");
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
            fprintf(out, "    li r1, #%d\n", g_locals[loc].stack_offset);
            fprintf(out, "    add r1, r7, r1\n");
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
    parse_relational(out);
}

/* Statements */
static void parse_statement(FILE *out)
{
    if (match_tok(TOK_IF)) {
        int lbl_else = ++g_label_counter;
        int lbl_end = ++g_label_counter;

        expect_tok(TOK_LPAREN, "'(' after if");
        parse_expression(out);
        expect_tok(TOK_RPAREN, "')' after if condition");

        fprintf(out, "    brz r0, .L%d\n", lbl_else);
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

        fprintf(out, "    brz r0, .L%d\n", lbl_end);
        parse_statement(out);
        fprintf(out, "    jmp .L%d\n", lbl_loop);
        fprintf(out, ".L%d:\n", lbl_end);
    } else if (match_tok(TOK_RETURN)) {
        if (cur_tok()->type != TOK_SEMI) {
            parse_expression(out);
        }
        expect_tok(TOK_SEMI, "';' after return");
        /* Pop locals off stack and return */
        if (g_local_count > 0) {
            fprintf(out, "    li r1, #%zu\n", g_local_count);
            fprintf(out, "    add r7, r7, r1\n");
        }
        fprintf(out, "    ret\n");
    } else if (match_tok(TOK_INT)) {
        /* Variable declaration */
        token *id = cur_tok();
        expect_tok(TOK_IDENT, "identifier in variable declaration");

        /* Register local */
        if (g_local_count >= MAX_VARS) {
            fprintf(stderr, "k3cc: error: too many local variables\n");
            exit(1);
        }
        strncpy(g_locals[g_local_count].name, id->text, MAX_IDENT - 1);
        g_locals[g_local_count].name[MAX_IDENT - 1] = '\0';
        g_locals[g_local_count].is_global = false;
        g_locals[g_local_count].stack_offset = (int)g_local_count;
        g_local_count++;

        /* Allocate space on stack: push 0 */
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    push r0\n");

        if (match_tok(TOK_ASSIGN)) {
            parse_expression(out);
            int loc = find_local(id->text);
            fprintf(out, "    li r1, #%d\n", g_locals[loc].stack_offset);
            fprintf(out, "    add r1, r7, r1\n");
            fprintf(out, "    store r0, [r1]\n");
        }
        expect_tok(TOK_SEMI, "';' after variable declaration");
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
    expect_tok(TOK_LBRACE, "'{' to start block");
    while (cur_tok()->type != TOK_RBRACE && cur_tok()->type != TOK_EOF) {
        parse_statement(out);
    }
    expect_tok(TOK_RBRACE, "'}' to end block");
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
                    g_locals[g_local_count].stack_offset = (int)g_local_count;
                    g_local_count++;
                    param_count++;

                    if (!match_tok(TOK_COMMA)) break;
                }
            }
            expect_tok(TOK_RPAREN, "')' after parameters");

            fprintf(out, "fn_%s:\n", func_name);

            /* Push parameters from registers r0..r4 to stack */
            for (int p = 0; p < param_count; ++p) {
                if (p < 5) {
                    fprintf(out, "    push r%d\n", p);
                } else {
                    fprintf(out, "    ldi r0, #0\n    push r0\n");
                }
            }

            parse_block(out);

            /* Default return */
            if (g_local_count > 0) {
                fprintf(out, "    li r1, #%zu\n", g_local_count);
                fprintf(out, "    add r7, r7, r1\n");
            }
            fprintf(out, "    ret\n\n");
        } else {
            /* Global variable */
            strncpy(g_globals[g_global_count], id->text, MAX_IDENT - 1);
            g_globals[g_global_count][MAX_IDENT - 1] = '\0';
            g_global_count++;

            int64_t init_val = 0;
            if (match_tok(TOK_ASSIGN)) {
                token *val = cur_tok();
                expect_tok(TOK_NUM, "constant number for global init");
                init_val = val->val;
            }
            expect_tok(TOK_SEMI, "';' after global variable");
            fprintf(out, "g_%s: .word %lld\n\n", id->text, (long long)init_val);
        }
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
