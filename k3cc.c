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
#define MAX_STRINGS 256
#define MAX_STR 1024 /* matches k3asm's quoted-string buffer */
#define MAX_SWITCH_CASES 128

typedef enum {
    TOK_EOF = 0,
    TOK_INT,
    TOK_IDENT,
    TOK_NUM,
    TOK_STR,        /* "..." string literal (raw escapes kept) */
    TOK_IF,
    TOK_ELSE,
    TOK_WHILE,
    TOK_DO,
    TOK_SWITCH,
    TOK_CASE,
    TOK_DEFAULT,
    TOK_FOR,
    TOK_RETURN,
    TOK_BREAK,
    TOK_CONTINUE,
    TOK_ASSIGN,     /* = */
    TOK_PLUSEQ,     /* += */
    TOK_MINUSEQ,    /* -= */
    TOK_STAREQ,     /* *= */
    TOK_SLASHEQ,    /* /= */
    TOK_PERCENTEQ,  /* %= */
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
    TOK_ANDAND,     /* && */
    TOK_OROR,       /* || */
    TOK_NOT,        /* ! */
    TOK_SIZEOF,     /* sizeof */
    TOK_ENUM,       /* enum */
    TOK_STRUCT,     /* struct */
    TOK_DOT,        /* . */
    TOK_AMP,      /* & */
    TOK_LPAREN,     /* ( */
    TOK_RPAREN,     /* ) */
    TOK_LBRACE,     /* { */
    TOK_RBRACE,     /* } */
    TOK_COMMA,      /* , */
    TOK_LBRACKET,   /* [ */
    TOK_RBRACKET,   /* ] */
    TOK_SEMI,       /* ; */
    TOK_QUESTION,   /* ? */
    TOK_COLON,      /* : */
    TOK_INC,        /* ++ */
    TOK_DEC         /* -- */
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
    int index;      /* 0 for 1st local, 1 for 2nd local, etc. */
    int size;       /* 1 for a scalar, N for an array of N words */
    int struct_idx; /* -1 for non-struct, >=0 index into g_structs */
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
static int g_global_struct_idx[MAX_VARS]; /* -1 unless a struct instance */

/* ── Struct type table ─────────────────────────────────────────────────────
 * Supports: struct Foo { int a; int b[4]; };
 * Member access:  var.field  (load)  or  var.field = expr  (store)
 * Each struct occupies (sum of member sizes) words in the frame/global data.
 */
#define MAX_STRUCTS  32
#define MAX_FIELDS   16

typedef struct {
    char name[MAX_IDENT];       /* field name */
    int  offset;                /* word offset from struct base */
    int  size;                  /* 1 for scalar, N for array */
} field_def;

typedef struct {
    char      name[MAX_IDENT];
    field_def fields[MAX_FIELDS];
    int       field_count;
    int       total_size;       /* sum of all field sizes */
} struct_def;

static struct_def g_structs[MAX_STRUCTS];
static int        g_struct_count = 0;

/* Look up a struct by name; returns index or -1. */
static int find_struct(const char *name)
{
    for (int i = 0; i < g_struct_count; ++i)
        if (strcmp(g_structs[i].name, name) == 0) return i;
    return -1;
}

/* Look up a field within a struct; returns its index or -1. */
static int find_field(int si, const char *name)
{
    for (int i = 0; i < g_structs[si].field_count; ++i)
        if (strcmp(g_structs[si].fields[i].name, name) == 0) return i;
    return -1;
}

/* String literals keep their raw source text (escapes included); k3asm
 * decodes them for `.string` directives. */
static char g_strings[MAX_STRINGS][MAX_STR];
static int g_string_count = 0;

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

/* Emit the base address of a struct field into r1.
 * `loc >= 0` addresses a local; otherwise `gname` names a global symbol
 * (emitted as `g_<gname>`).  `foff` is the field's word offset within the
 * struct.  For globals with a non-zero offset r0 is clobbered, so callers
 * must compute the base before parsing an array index. */
static void gen_field_addr(FILE *out, const char *gname, int loc, int foff)
{
    if (loc >= 0) {
        int offset = -(g_locals[loc].index + g_locals[loc].size) + foff;
        fprintf(out, "    li r1, #%d\n", offset);
        fprintf(out, "    add r1, r3, r1\n");
    } else {
        fprintf(out, "    li r1, g_%s\n", gname);
        if (foff > 0) {
            fprintf(out, "    li r0, #%d\n", foff);
            fprintf(out, "    add r1, r1, r0\n");
        }
    }
}

/* Decode one backslash escape from a character or string literal. */
static int decode_escape_char(char c)
{
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '0': return '\0';
    case 'e': return 27;
    case '\\': return '\\';
    case '"': return '"';
    case '\'': return '\'';
    default: return (unsigned char)c;
    }
}

#define MAX_MACROS 256
typedef struct {
    char name[MAX_IDENT];
    int64_t val;
} macro_def;

static macro_def g_macros[MAX_MACROS];
static int g_macro_count = 0;

static int find_macro(const char *name)
{
    for (int i = 0; i < g_macro_count; ++i) {
        if (strcmp(g_macros[i].name, name) == 0) return i;
    }
    return -1;
}

/* Lexer */
static void lex_internal(const char *src, const char *basedir, int depth)
{
    const char *p = src;
    int line = 1;

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

        /* Preprocessor directives (#include, #define, header guards) */
        if (*p == '#') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (strncmp(p, "include", 7) == 0 &&
                (p[7] == ' ' || p[7] == '\t' || p[7] == '"' || p[7] == '<')) {
                p += 7;
                while (*p == ' ' || *p == '\t') p++;
                char delim = *p;
                if (delim == '"' || delim == '<') {
                    char end_delim = (delim == '"') ? '"' : '>';
                    p++;
                    char inc_name[512];
                    size_t k = 0;
                    while (*p && *p != end_delim && *p != '\n' &&
                           k < sizeof(inc_name) - 1) {
                        inc_name[k++] = *p++;
                    }
                    inc_name[k] = '\0';
                    if (*p == end_delim) p++;

                    char full_path[1024];
                    if (basedir != NULL && basedir[0] != '\0' && inc_name[0] != '/') {
                        snprintf(full_path, sizeof(full_path), "%s/%s", basedir, inc_name);
                    } else {
                        snprintf(full_path, sizeof(full_path), "%s", inc_name);
                    }

                    FILE *finc = fopen(full_path, "rb");
                    if (!finc) {
                        finc = fopen(inc_name, "rb");
                    }
                    if (finc) {
                        fseek(finc, 0, SEEK_END);
                        long isz = ftell(finc);
                        fseek(finc, 0, SEEK_SET);
                        char *isrc = (char *)malloc((size_t)isz + 1);
                        if (isrc) {
                            if (fread(isrc, 1, (size_t)isz, finc) == (size_t)isz) {
                                isrc[isz] = '\0';
                                char new_basedir[1024];
                                strncpy(new_basedir, full_path, sizeof(new_basedir) - 1);
                                new_basedir[sizeof(new_basedir) - 1] = '\0';
                                char *s = strrchr(new_basedir, '/');
                                if (s) *s = '\0'; else new_basedir[0] = '\0';

                                if (depth < 8) {
                                    lex_internal(isrc, new_basedir, depth + 1);
                                }
                            }
                            free(isrc);
                        }
                        fclose(finc);
                    }
                }
            } else if (strncmp(p, "define", 6) == 0 &&
                       (p[6] == ' ' || p[6] == '\t')) {
                p += 6;
                while (*p == ' ' || *p == '\t') p++;
                if (isalpha((unsigned char)*p) || *p == '_') {
                    char mname[MAX_IDENT];
                    size_t k = 0;
                    while ((isalnum((unsigned char)*p) || *p == '_') &&
                           k < sizeof(mname) - 1) {
                        mname[k++] = *p++;
                    }
                    mname[k] = '\0';
                    while (*p == ' ' || *p == '\t') p++;
                    if (isdigit((unsigned char)*p) || *p == '-' || *p == '+' || *p == '\'') {
                        int64_t mval = 0;
                        if (*p == '\'') {
                            p++;
                            mval = (unsigned char)*p++;
                            if (*p == '\'') p++;
                        } else {
                            char *end = NULL;
                            mval = strtoll(p, &end, 10);
                            p = end;
                        }
                        if (g_macro_count < MAX_MACROS) {
                            int existing = find_macro(mname);
                            int idx = (existing >= 0) ? existing : g_macro_count++;
                            snprintf(g_macros[idx].name, sizeof(g_macros[idx].name), "%s", mname);
                            g_macros[idx].val = mval;
                        }
                    }
                }
            }
            while (*p && *p != '\n') p++;
            continue;
        }

        token *tok = &g_tokens[g_token_count++];
        tok->line = line;
        tok->text[0] = '\0';
        tok->val = 0;

        if (*p == '"') {
            if (g_string_count >= MAX_STRINGS) {
                fprintf(stderr, "k3cc: error: too many string literals "
                                "(maximum %d)\n", MAX_STRINGS);
                exit(1);
            }
            int idx = g_string_count;
            size_t n = 0;
            p++;
            while (*p && *p != '"') {
                if (*p == '\n') line++;
                if (*p == '\\' && *(p + 1) != '\0') {
                    if (n + 2 >= MAX_STR) {
                        fprintf(stderr, "k3cc:%d: error: string literal too "
                                        "long (maximum %d bytes)\n",
                                line, MAX_STR - 1);
                        exit(1);
                    }
                    g_strings[idx][n++] = *p++;
                    g_strings[idx][n++] = *p++;
                    continue;
                }
                if (n + 1 >= MAX_STR) {
                    fprintf(stderr, "k3cc:%d: error: string literal too long "
                                    "(maximum %d bytes)\n",
                            line, MAX_STR - 1);
                    exit(1);
                }
                g_strings[idx][n++] = *p++;
            }
            if (*p != '"') {
                fprintf(stderr, "k3cc:%d: error: unterminated string literal\n",
                        line);
                exit(1);
            }
            p++;
            g_strings[idx][n] = '\0';
            tok->type = TOK_STR;
            tok->val = idx;
            g_string_count++;
            continue;
        }

        if (*p == '\'') {
            p++;
            int c;
            if (*p == '\\' && *(p + 1) != '\0') {
                p++;
                c = decode_escape_char(*p++);
            } else if (*p == '\'' || *p == '\0') {
                fprintf(stderr, "k3cc:%d: error: empty or unterminated "
                                "character literal\n", line);
                exit(1);
            } else {
                c = (unsigned char)*p++;
            }
            if (*p != '\'') {
                fprintf(stderr,
                        "k3cc:%d: error: unterminated character literal\n",
                        line);
                exit(1);
            }
            p++;
            tok->type = TOK_NUM;
            tok->val = c;
            continue;
        }

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

            int mi = find_macro(tok->text);
            if (mi >= 0) {
                tok->type = TOK_NUM;
                tok->val = g_macros[mi].val;
                continue;
            }

            if (strcmp(tok->text, "int") == 0) tok->type = TOK_INT;
            else if (strcmp(tok->text, "const") == 0) tok->type = TOK_INT;
            else if (strcmp(tok->text, "enum") == 0) tok->type = TOK_INT;
            else if (strcmp(tok->text, "if") == 0) tok->type = TOK_IF;
            else if (strcmp(tok->text, "else") == 0) tok->type = TOK_ELSE;
            else if (strcmp(tok->text, "while") == 0) tok->type = TOK_WHILE;
            else if (strcmp(tok->text, "do") == 0) tok->type = TOK_DO;
            else if (strcmp(tok->text, "switch") == 0) tok->type = TOK_SWITCH;
            else if (strcmp(tok->text, "case") == 0) tok->type = TOK_CASE;
            else if (strcmp(tok->text, "default") == 0) tok->type = TOK_DEFAULT;
            else if (strcmp(tok->text, "for") == 0) tok->type = TOK_FOR;
            else if (strcmp(tok->text, "return") == 0) tok->type = TOK_RETURN;
            else if (strcmp(tok->text, "break") == 0) tok->type = TOK_BREAK;
            else if (strcmp(tok->text, "continue") == 0) tok->type = TOK_CONTINUE;
            else if (strcmp(tok->text, "sizeof") == 0) tok->type = TOK_SIZEOF;
            else if (strcmp(tok->text, "struct") == 0) tok->type = TOK_STRUCT;
            else tok->type = TOK_IDENT;
            continue;
        }

        if (*p == '=' && *(p + 1) == '=') { tok->type = TOK_EQ; p += 2; continue; }
        if (*p == '!' && *(p + 1) == '=') { tok->type = TOK_NEQ; p += 2; continue; }
        if (*p == '&' && *(p + 1) == '&') { tok->type = TOK_ANDAND; p += 2; continue; }
        if (*p == '|' && *(p + 1) == '|') { tok->type = TOK_OROR; p += 2; continue; }
        if (*p == '+' && *(p + 1) == '=') { tok->type = TOK_PLUSEQ; p += 2; continue; }
        if (*p == '-' && *(p + 1) == '=') { tok->type = TOK_MINUSEQ; p += 2; continue; }
        if (*p == '*' && *(p + 1) == '=') { tok->type = TOK_STAREQ; p += 2; continue; }
        if (*p == '/' && *(p + 1) == '=') { tok->type = TOK_SLASHEQ; p += 2; continue; }
        if (*p == '%' && *(p + 1) == '=') { tok->type = TOK_PERCENTEQ; p += 2; continue; }
        if (*p == '+' && *(p + 1) == '+') { tok->type = TOK_INC; p += 2; continue; }
        if (*p == '-' && *(p + 1) == '-') { tok->type = TOK_DEC; p += 2; continue; }
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
        case '!': tok->type = TOK_NOT; p++; break;
        case '&': tok->type = TOK_AMP; p++; break;
        case '(': tok->type = TOK_LPAREN; p++; break;
        case ')': tok->type = TOK_RPAREN; p++; break;
        case '{': tok->type = TOK_LBRACE; p++; break;
        case '}': tok->type = TOK_RBRACE; p++; break;
        case ',': tok->type = TOK_COMMA; p++; break;
        case '[': tok->type = TOK_LBRACKET; p++; break;
        case ']': tok->type = TOK_RBRACKET; p++; break;
        case ';': tok->type = TOK_SEMI; p++; break;
        case '?': tok->type = TOK_QUESTION; p++; break;
        case ':': tok->type = TOK_COLON; p++; break;
        case '.': tok->type = TOK_DOT;   p++; break;
        default:
            fprintf(stderr, "k3cc:%d: error: unexpected character '%c'\n", line, *p);
            exit(1);
        }
    }
}

static void lex(const char *src, const char *basedir)
{
    g_token_count = 0;
    g_macro_count = 0;
    lex_internal(src, basedir, 0);
    g_tokens[g_token_count].type = TOK_EOF;
    g_tokens[g_token_count].line = 1;
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
static void emit_cond_jump(FILE *out, const char *cond, int label);

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

/* Parse a scalar or array-element lvalue, leaving its current value in r0 and
 * its address in r1.  Used by prefix ++/-- and postfix ++/--. */
static void parse_lvalue_load(FILE *out)
{
    token *id = cur_tok();
    expect_tok(TOK_IDENT, "variable after ++/--");

    char name[MAX_IDENT];
    strncpy(name, id->text, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    int loc = find_local(name);
    if (match_tok(TOK_LBRACKET)) {
        int gl = find_global(name);
        if (loc < 0 && gl < 0) {
            fprintf(stderr, "k3cc:%d: error: undeclared array '%s'\n",
                    id->line, name);
            exit(1);
        }
        if ((loc >= 0 && g_locals[loc].size <= 1) ||
            (loc < 0 && g_global_sizes[gl] <= 1)) {
            /* Pointer indexing: ptr[index] */
            parse_expression(out); /* index in r0 */
            expect_tok(TOK_RBRACKET, "']' after array index");
            fprintf(out, "    push r0\n"); /* save index */
            if (loc >= 0) {
                gen_local_addr(out, loc);
            } else {
                fprintf(out, "    li r1, g_%s\n", name);
            }
            fprintf(out, "    load r1, [r1]\n"); /* r1 = pointer base */
            fprintf(out, "    pop r0\n"); /* r0 = index */
            fprintf(out, "    add r1, r1, r0\n"); /* r1 = pointer + index */
            fprintf(out, "    load r0, [r1]\n");
        } else {
            parse_expression(out);
            expect_tok(TOK_RBRACKET, "']' after array index");
            gen_array_addr(out, name, loc);
            fprintf(out, "    load r0, [r1]\n");
        }
    } else if (loc >= 0) {
        gen_local_addr(out, loc);
        fprintf(out, "    load r0, [r1]\n");
    } else if (is_global(name)) {
        fprintf(out, "    li r1, g_%s\n", name);
        fprintf(out, "    load r0, [r1]\n");
    } else {
        fprintf(stderr, "k3cc:%d: error: undeclared variable '%s'\n",
                id->line, name);
        exit(1);
    }
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
    } else if (match_tok(TOK_NOT)) {
        parse_primary(out);
        int lbl_false = ++g_label_counter;
        int lbl_end = ++g_label_counter;
        fprintf(out, "    brz r0, .L%d\n", lbl_false);
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    jmp .L%d\n", lbl_end);
        fprintf(out, ".L%d:\n", lbl_false);
        fprintf(out, "    ldi r0, #1\n");
        fprintf(out, ".L%d:\n", lbl_end);
    } else if (match_tok(TOK_STAR)) {
        parse_primary(out);
        fprintf(out, "    mov r1, r0\n");
        fprintf(out, "    load r0, [r1]\n");
    } else if (match_tok(TOK_AMP)) {
        /* Address-of: &var or &arr[index] */
        token *id = cur_tok();
        expect_tok(TOK_IDENT, "variable after '&'");
        char name[MAX_IDENT];
        strncpy(name, id->text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';

        int loc = find_local(name);
        if (match_tok(TOK_LBRACKET)) {
            parse_expression(out); /* index into r0 */
            expect_tok(TOK_RBRACKET, "']' after array index");
            gen_array_addr(out, name, loc);
            fprintf(out, "    mov r0, r1\n");
        } else if (loc >= 0) {
            gen_local_addr(out, loc);
            fprintf(out, "    mov r0, r1\n");
        } else if (is_global(name)) {
            fprintf(out, "    li r0, g_%s\n", name);
        } else {
            fprintf(stderr, "k3cc:%d: error: undeclared variable '%s' in '&'\n", id->line, name);
            exit(1);
        }
    } else if (match_tok(TOK_SIZEOF)) {
        /* sizeof type: r0 = size of type (1 word) */
        fprintf(out, "    li r0, #1\n");
    } else if (match_tok(TOK_INC)) {
        parse_lvalue_load(out);
        fprintf(out, "    addi r0, #1\n");
        fprintf(out, "    store r0, [r1]\n");
    } else if (match_tok(TOK_DEC)) {
        parse_lvalue_load(out);
        fprintf(out, "    addi r0, #-1\n");
        fprintf(out, "    store r0, [r1]\n");
    } else if (match_tok(TOK_NUM)) {
        fprintf(out, "    li r0, #%lld\n", (long long)tok->val);
    } else if (tok->type == TOK_IDENT) {
        char name[MAX_IDENT];
        strncpy(name, tok->text, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        advance_tok();

        /* Function Call */
        if (match_tok(TOK_LPAREN)) {
            /* puts() takes a string literal directly; strings are not
             * first-class values, so this is the only place they appear. */
            if (strcmp(name, "puts") == 0) {
                token *s = cur_tok();
                if (s->type != TOK_STR) {
                    fprintf(stderr,
                            "k3cc:%d: error: puts() expects a string literal\n",
                            s->line);
                    exit(1);
                }
                advance_tok();
                expect_tok(TOK_RPAREN, "')' after puts() argument");
                fprintf(out, "    li r0, __str_%lld\n", (long long)s->val);
                fprintf(out, "    trap #4\n");      /* SYS_PUTS */
                return;
            }
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
                if ((loc >= 0 && g_locals[loc].size <= 1) ||
                    (loc < 0 && g_global_sizes[gl] <= 1)) {
                    /* Pointer indexing: ptr[index] */
                    parse_expression(out); /* index in r0 */
                    expect_tok(TOK_RBRACKET, "']' after array index");
                    fprintf(out, "    push r0\n"); /* save index */
                    if (loc >= 0) {
                        gen_local_addr(out, loc);
                    } else {
                        fprintf(out, "    li r1, g_%s\n", name);
                    }
                    fprintf(out, "    load r1, [r1]\n"); /* r1 = pointer base */
                    fprintf(out, "    pop r0\n"); /* r0 = index */
                    fprintf(out, "    add r1, r1, r0\n"); /* r1 = pointer + index */
                    fprintf(out, "    load r0, [r1]\n");
                } else {
                    parse_expression(out);
                    expect_tok(TOK_RBRACKET, "']' after array index");
                    gen_array_addr(out, name, loc);
                    fprintf(out, "    load r0, [r1]\n");
                }
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

            /* Postfix ++/--: the value of the expression is the old value. */
            if (match_tok(TOK_INC)) {
                fprintf(out, "    push r0\n");
                fprintf(out, "    addi r0, #1\n");
                fprintf(out, "    store r0, [r1]\n");
                fprintf(out, "    pop r0\n");
            } else if (match_tok(TOK_DEC)) {
                fprintf(out, "    push r0\n");
                fprintf(out, "    addi r0, #-1\n");
                fprintf(out, "    store r0, [r1]\n");
                fprintf(out, "    pop r0\n");
            } else if (cur_tok()->type == TOK_DOT) {
                /* Member access: var.field  →  r0 = value at (base + offset) */
                advance_tok(); /* consume '.' */
                token *fname = cur_tok();
                expect_tok(TOK_IDENT, "field name after '.'");

                /* Look up struct_idx from the variable (local or global) */
                int v_loc = find_local(name);
                int v_gl  = (v_loc >= 0) ? -1 : find_global(name);
                int v_si;
                if (v_loc >= 0) {
                    v_si = g_locals[v_loc].struct_idx;
                } else if (v_gl >= 0) {
                    v_si = g_global_struct_idx[v_gl];
                } else {
                    v_si = -1;
                }
                if (v_si < 0) {
                    fprintf(stderr, "k3cc:%d: error: '%s' is not a struct\n",
                            fname->line, name);
                    exit(1);
                }
                int fi = find_field(v_si, fname->text);
                if (fi < 0) {
                    fprintf(stderr, "k3cc:%d: error: struct '%s' has no field '%s'\n",
                            fname->line, g_structs[v_si].name, fname->text);
                    exit(1);
                }
                int foff = g_structs[v_si].fields[fi].offset;
                int fsize = g_structs[v_si].fields[fi].size;

                if (cur_tok()->type == TOK_LBRACKET) {
                    /* Array field: var.field[index] */
                    if (fsize <= 1) {
                        fprintf(stderr, "k3cc:%d: error: field '%s' is not an "
                                        "array\n", fname->line, fname->text);
                        exit(1);
                    }
                    gen_field_addr(out, name, v_loc, foff);
                    fprintf(out, "    push r1\n");       /* save base */
                    advance_tok(); /* '[' */
                    parse_expression(out); /* index into r0 */
                    expect_tok(TOK_RBRACKET, "']' after field index");
                    fprintf(out, "    pop r1\n");
                    fprintf(out, "    add r1, r1, r0\n");
                    fprintf(out, "    load r0, [r1]\n");
                } else {
                    gen_field_addr(out, name, v_loc, foff);
                    fprintf(out, "    load r0, [r1]\n");
                }
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

/* Logical AND: short-circuit, yields 0 or 1. */
static void parse_logical_and(FILE *out)
{
    parse_relational(out);
    while (match_tok(TOK_ANDAND)) {
        int lbl_false = ++g_label_counter;
        int lbl_end = ++g_label_counter;
        emit_cond_jump(out, "brnz", lbl_false);
        parse_relational(out);
        emit_cond_jump(out, "brnz", lbl_false);
        fprintf(out, "    ldi r0, #1\n");
        fprintf(out, "    jmp .L%d\n", lbl_end);
        fprintf(out, ".L%d:\n", lbl_false);
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, ".L%d:\n", lbl_end);
    }
}

/* Logical OR: short-circuit, yields 0 or 1. */
static void parse_logical_or(FILE *out)
{
    parse_logical_and(out);
    while (match_tok(TOK_OROR)) {
        int lbl_true = ++g_label_counter;
        int lbl_end = ++g_label_counter;
        emit_cond_jump(out, "brz", lbl_true);
        parse_logical_and(out);
        emit_cond_jump(out, "brz", lbl_true);
        fprintf(out, "    ldi r0, #0\n");
        fprintf(out, "    jmp .L%d\n", lbl_end);
        fprintf(out, ".L%d:\n", lbl_true);
        fprintf(out, "    ldi r0, #1\n");
        fprintf(out, ".L%d:\n", lbl_end);
    }
}

/* Conditional (ternary): cond ? then : else.  Both arms leave r0 set. */
static void parse_conditional(FILE *out)
{
    parse_logical_or(out);
    if (match_tok(TOK_QUESTION)) {
        int lbl_else = ++g_label_counter;
        int lbl_end = ++g_label_counter;
        emit_cond_jump(out, "brnz", lbl_else); /* if cond == 0 goto else */
        parse_expression(out);
        expect_tok(TOK_COLON, "':' in conditional expression");
        fprintf(out, "    jmp .L%d\n", lbl_end);
        fprintf(out, ".L%d:\n", lbl_else);
        parse_expression(out);
        fprintf(out, ".L%d:\n", lbl_end);
    }
}

static bool is_compound_op(token_type t)
{
    return t == TOK_PLUSEQ || t == TOK_MINUSEQ || t == TOK_STAREQ ||
           t == TOK_SLASHEQ || t == TOK_PERCENTEQ;
}

/* Emit `r0 = r0 <op> r1` for a compound assignment operator. */
static void emit_compound_op(FILE *out, token_type op)
{
    if (op == TOK_PLUSEQ) {
        fprintf(out, "    add r0, r0, r1\n");
    } else if (op == TOK_MINUSEQ) {
        fprintf(out, "    sub r0, r0, r1\n");
    } else if (op == TOK_STAREQ) {
        fprintf(out, "    mul r0, r0, r1\n");
    } else {
        g_uses_divmod = true;
        fprintf(out, "    push r6\n");
        fprintf(out, "    push r3\n");
        fprintf(out, "    call __k3_divmod\n");
        fprintf(out, "    pop r3\n");
        fprintf(out, "    pop r6\n");
        if (op == TOK_PERCENTEQ) {
            fprintf(out, "    mov r0, r1\n");
        }
    }
}

/* If the cursor is `IDENT [ ... ] op` where op is `=` or a compound
 * assignment, return that operator; otherwise return TOK_EOF. */
static token_type array_assign_op(void)
{
    if (cur_tok()->type != TOK_IDENT ||
        g_tokens[g_token_pos + 1].type != TOK_LBRACKET) {
        return TOK_EOF;
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
    if (g_tokens[i].type != TOK_RBRACKET) {
        return TOK_EOF;
    }
    token_type op = g_tokens[i + 1].type;
    if (op == TOK_ASSIGN || is_compound_op(op)) {
        return op;
    }
    return TOK_EOF;
}

/* Detect `*IDENT op` or `*( ... ) op` where op is `=` or a compound op.
 * Returns the operator token or TOK_EOF.  The cursor is left unchanged. */
static token_type deref_assign_op(void)
{
    if (cur_tok()->type != TOK_STAR) {
        return TOK_EOF;
    }
    /* Case 1: *IDENT op */
    if (g_tokens[g_token_pos + 1].type == TOK_IDENT) {
        token_type op = g_tokens[g_token_pos + 2].type;
        if (op == TOK_ASSIGN || is_compound_op(op)) {
            return op;
        }
    }
    /* Case 2: *( ... ) op */
    if (g_tokens[g_token_pos + 1].type == TOK_LPAREN) {
        size_t i = g_token_pos + 2;
        int depth = 1;
        while (i < g_token_count && g_tokens[i].type != TOK_EOF) {
            if (g_tokens[i].type == TOK_LPAREN) depth++;
            else if (g_tokens[i].type == TOK_RPAREN) {
                depth--;
                if (depth == 0) break;
            }
            i++;
        }
        if (i < g_token_count && g_tokens[i].type == TOK_RPAREN) {
            token_type op = g_tokens[i + 1].type;
            if (op == TOK_ASSIGN || is_compound_op(op)) {
                return op;
            }
        }
    }
    return TOK_EOF;
}

/* Detect `IDENT . IDENT [ ... ] op` where op is `=` or a compound op.
 * Returns the operator token or TOK_EOF.  The cursor is left unchanged. */
static token_type member_array_assign_op(void)
{
    if (g_tokens[g_token_pos].type != TOK_IDENT) return TOK_EOF;
    if (g_tokens[g_token_pos + 1].type != TOK_DOT) return TOK_EOF;
    if (g_tokens[g_token_pos + 2].type != TOK_IDENT) return TOK_EOF;
    if (g_tokens[g_token_pos + 3].type != TOK_LBRACKET) return TOK_EOF;

    size_t i = g_token_pos + 3;
    int depth = 0;
    while (i < g_token_count) {
        if (g_tokens[i].type == TOK_LBRACKET) {
            depth++;
        } else if (g_tokens[i].type == TOK_RBRACKET) {
            depth--;
            if (depth == 0) break;
        }
        i++;
    }
    if (g_tokens[i].type != TOK_RBRACKET) return TOK_EOF;
    token_type op = g_tokens[i + 1].type;
    if (op == TOK_ASSIGN || is_compound_op(op)) return op;
    return TOK_EOF;
}

/* Assignment and Full Expression */
static void parse_expression(FILE *out)
{
    /* Pointer assignment: *ptr = expr or *(ptr + offset) = expr */
    token_type dop = deref_assign_op();
    if (dop != TOK_EOF) {
        advance_tok(); /* consume '*' */

        if (cur_tok()->type == TOK_LPAREN) {
            advance_tok(); /* '(' */
            parse_expression(out); /* target pointer address in r0 */
            expect_tok(TOK_RPAREN, "')' after pointer expression");
        } else {
            token *id = cur_tok();
            char name[MAX_IDENT];
            strncpy(name, id->text, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            advance_tok(); /* ident */

            int loc = find_local(name);
            if (loc >= 0) {
                gen_local_addr(out, loc);
                fprintf(out, "    load r0, [r1]\n");
            } else if (is_global(name)) {
                fprintf(out, "    li r1, g_%s\n", name);
                fprintf(out, "    load r0, [r1]\n");
            } else {
                fprintf(stderr, "k3cc:%d: error: undeclared pointer '%s'\n", id->line, name);
                exit(1);
            }
        }

        fprintf(out, "    mov r1, r0\n");
        fprintf(out, "    push r1\n"); /* save target address */

        if (dop != TOK_ASSIGN) {
            fprintf(out, "    load r0, [r1]\n");
            fprintf(out, "    push r0\n"); /* save lhs value */
        }

        expect_tok(dop, "assignment operator after pointer dereference");
        parse_expression(out); /* RHS value in r0 */

        if (dop != TOK_ASSIGN) {
            fprintf(out, "    mov r1, r0\n");
            fprintf(out, "    pop r0\n");
            emit_compound_op(out, dop);
        }

        fprintf(out, "    pop r1\n"); /* recover target address */
        fprintf(out, "    store r0, [r1]\n");
        return;
    }

    /* Member array assignment: var.field[index] = expr */
    if (member_array_assign_op() != TOK_EOF) {
        char vname[MAX_IDENT];
        strncpy(vname, cur_tok()->text, MAX_IDENT - 1);
        vname[MAX_IDENT - 1] = '\0';
        int v_loc = find_local(vname);
        int v_gl  = (v_loc >= 0) ? -1 : find_global(vname);
        int v_si  = (v_loc >= 0) ? g_locals[v_loc].struct_idx
                                 : (v_gl >= 0 ? g_global_struct_idx[v_gl] : -1);
        if (v_si < 0) {
            fprintf(stderr, "k3cc:%d: error: '%s' is not a struct\n",
                    cur_tok()->line, vname);
            exit(1);
        }

        advance_tok(); /* var name */
        advance_tok(); /* '.' */
        token *fname = cur_tok();
        advance_tok(); /* field name */
        advance_tok(); /* '[' */

        int fi = find_field(v_si, fname->text);
        if (fi < 0) {
            fprintf(stderr, "k3cc:%d: error: struct '%s' has no field '%s'\n",
                    fname->line, g_structs[v_si].name, fname->text);
            exit(1);
        }
        int foff = g_structs[v_si].fields[fi].offset;

        gen_field_addr(out, vname, v_loc, foff);
        fprintf(out, "    push r1\n");   /* save field base */
        parse_expression(out); /* index into r0 */
        expect_tok(TOK_RBRACKET, "']' after field index");
        /* element address in r1 = base + index */
        fprintf(out, "    pop r1\n");
        fprintf(out, "    add r1, r1, r0\n");
        fprintf(out, "    push r1\n");   /* save element address */

        token_type op = cur_tok()->type;
        if (op != TOK_ASSIGN) {
            fprintf(out, "    load r0, [r1]\n");
            fprintf(out, "    push r0\n");
        }
        expect_tok(op, "assignment operator after field subscript");
        parse_expression(out); /* value in r0 */
        if (op != TOK_ASSIGN) {
            fprintf(out, "    mov r1, r0\n");
            fprintf(out, "    pop r0\n");
            emit_compound_op(out, op);
        }
        fprintf(out, "    pop r1\n");
        fprintf(out, "    store r0, [r1]\n");
        return;
    }

    /* Member assignment: var.field = expr */
    if (cur_tok()->type == TOK_IDENT &&
        g_tokens[g_token_pos + 1].type == TOK_DOT) {

        char vname[MAX_IDENT];
        strncpy(vname, cur_tok()->text, MAX_IDENT - 1);
        vname[MAX_IDENT - 1] = '\0';
        int v_loc = find_local(vname);
        int v_gl  = (v_loc >= 0) ? -1 : find_global(vname);
        int v_si  = (v_loc >= 0) ? g_locals[v_loc].struct_idx
                                 : (v_gl >= 0 ? g_global_struct_idx[v_gl] : -1);

        /* Only intercept if this really is a struct member store */
        if (v_si >= 0 &&
            g_tokens[g_token_pos + 2].type == TOK_IDENT &&
            (g_tokens[g_token_pos + 3].type == TOK_ASSIGN ||
             is_compound_op(g_tokens[g_token_pos + 3].type))) {

            advance_tok(); /* var name */
            advance_tok(); /* '.' */

            token *fname = cur_tok();
            advance_tok(); /* field name */

            token_type op = cur_tok()->type;
            advance_tok(); /* '=' or compound op */

            int fi = find_field(v_si, fname->text);
            if (fi < 0) {
                fprintf(stderr, "k3cc:%d: error: struct '%s' has no field '%s'\n",
                        fname->line, g_structs[v_si].name, fname->text);
                exit(1);
            }
            int foff = g_structs[v_si].fields[fi].offset;

            /* Compute the field address first (globals may clobber r0),
             * then stash it so the RHS can use r0 freely. */
            gen_field_addr(out, vname, v_loc, foff);
            fprintf(out, "    push r1\n");           /* save field address */

            if (op != TOK_ASSIGN) {
                /* Load current value for compound ops */
                fprintf(out, "    load r0, [r1]\n");
                fprintf(out, "    push r0\n");       /* save lhs value */
            }

            parse_expression(out); /* RHS in r0 */

            if (op != TOK_ASSIGN) {
                fprintf(out, "    mov r1, r0\n");
                fprintf(out, "    pop r0\n");
                emit_compound_op(out, op);
            }

            fprintf(out, "    pop r1\n");            /* recover field address */
            fprintf(out, "    store r0, [r1]\n");
            return;
        }
    }

    if (cur_tok()->type == TOK_IDENT) {
        token_type op = g_tokens[g_token_pos + 1].type;
        if (op == TOK_ASSIGN || is_compound_op(op)) {
            char name[MAX_IDENT];
            strncpy(name, cur_tok()->text, sizeof(name) - 1);
            name[sizeof(name) - 1] = '\0';
            advance_tok(); /* ident */
            advance_tok(); /* assignment operator */

            int loc = find_local(name);
            if (loc < 0 && !is_global(name)) {
                fprintf(stderr,
                        "k3cc: error: assignment to undeclared variable '%s'\n",
                        name);
                exit(1);
            }

            if (op != TOK_ASSIGN) {
                /* Load the current value as the left operand. */
                if (loc >= 0) {
                    gen_local_addr(out, loc);
                } else {
                    fprintf(out, "    li r1, g_%s\n", name);
                }
                fprintf(out, "    load r0, [r1]\n");
                fprintf(out, "    push r0\n");
            }

            parse_expression(out); /* right-hand side in r0 */

            if (op != TOK_ASSIGN) {
                fprintf(out, "    mov r1, r0\n");
                fprintf(out, "    pop r0\n");
                emit_compound_op(out, op);
            }

            if (loc >= 0) {
                gen_local_addr(out, loc);
            } else {
                fprintf(out, "    li r1, g_%s\n", name);
            }
            fprintf(out, "    store r0, [r1]\n");
            return;
        }
    }

    token_type arr_op = array_assign_op();
    if (arr_op != TOK_EOF) {
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
            /* Pointer indexing assignment: ptr[index] = expr */
            parse_expression(out); /* index in r0 */
            expect_tok(TOK_RBRACKET, "']' after array index");
            fprintf(out, "    push r0\n"); /* save index */
            if (loc >= 0) {
                gen_local_addr(out, loc);
            } else {
                fprintf(out, "    li r1, g_%s\n", name);
            }
            fprintf(out, "    load r1, [r1]\n"); /* r1 = pointer base */
            fprintf(out, "    pop r0\n"); /* r0 = index */
            fprintf(out, "    add r1, r1, r0\n"); /* r1 = pointer + index */
            fprintf(out, "    push r1\n"); /* save element address */
        } else {
            parse_expression(out); /* index in r0 */
            expect_tok(TOK_RBRACKET, "']' after array index");
            gen_array_addr(out, name, loc);
            fprintf(out, "    push r1\n");   /* save element address */
        }
        if (arr_op != TOK_ASSIGN) {
            fprintf(out, "    load r0, [r1]\n");
            fprintf(out, "    push r0\n");
        }
        expect_tok(arr_op, "assignment operator after array subscript");
        parse_expression(out);           /* value in r0 */
        if (arr_op != TOK_ASSIGN) {
            fprintf(out, "    mov r1, r0\n");
            fprintf(out, "    pop r0\n");
            emit_compound_op(out, arr_op);
        }
        fprintf(out, "    pop r1\n");
        fprintf(out, "    store r0, [r1]\n");
        return;
    }

    /* Comma operator support */
    if (cur_tok()->type == TOK_COMMA) {
        advance_tok(); /* consume comma */
        parse_expression(out); /* evaluate right-hand side; r0 has its value */
    }
    parse_conditional(out);
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
    /* struct Foo varname; or struct Foo varname[N]; */
    if (cur_tok()->type == TOK_IDENT) {
        int si = find_struct(cur_tok()->text);
        if (si >= 0) {
            advance_tok(); /* consume struct tag */
            token *svar = cur_tok();
            expect_tok(TOK_IDENT, "variable name after struct tag");

            int arr_count = 1;  /* number of struct instances (1 = single) */
            if (match_tok(TOK_LBRACKET)) {
                token *cn = cur_tok();
                if (cn->type != TOK_NUM || cn->val <= 0) {
                    fprintf(stderr, "k3cc:%d: error: struct array size must be "
                                    "a positive constant\n", cn->line);
                    exit(1);
                }
                arr_count = (int)cn->val;
                advance_tok();
                expect_tok(TOK_RBRACKET, "']' after struct array size");
            }

            int total = g_structs[si].total_size * arr_count;

            if (find_local(svar->text) >= 0) {
                fprintf(stderr, "k3cc:%d: error: variable '%s' already declared\n",
                        svar->line, svar->text);
                exit(1);
            }
            if (g_local_count >= MAX_VARS) {
                fprintf(stderr, "k3cc: error: too many local variables\n");
                exit(1);
            }
            strncpy(g_locals[g_local_count].name, svar->text, MAX_IDENT - 1);
            g_locals[g_local_count].name[MAX_IDENT - 1] = '\0';
            g_locals[g_local_count].is_global = false;
            g_locals[g_local_count].index = (int)g_frame_words;
            g_locals[g_local_count].size = total;
            g_locals[g_local_count].struct_idx = si;
            g_local_count++;
            g_frame_words += (size_t)total;

            /* Reserve total words, zero-initialised */
            fprintf(out, "    li r1, #%d\n", total);
            fprintf(out, "    sub r7, r7, r1\n");
            /* zero-fill */
            for (int w = 0; w < total; ++w) {
                int off = -(g_locals[g_local_count - 1].index + w + 1);
                fprintf(out, "    ldi r0, #0\n");
                fprintf(out, "    li r1, #%d\n", off);
                fprintf(out, "    add r1, r3, r1\n");
                fprintf(out, "    store r0, [r1]\n");
            }
            expect_tok(TOK_SEMI, "';' after struct variable declaration");
            return;
        }
    }

    match_tok(TOK_STAR); /* allow pointer declaration: int *p; */
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
    g_locals[g_local_count].struct_idx = -1;
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

/* Scan a switch body for top-level `case`/`default` labels and assign each a
 * fresh label in source order.  Returns the token index of the closing `}`. */
static size_t scan_switch_body(size_t start, int64_t *values, int *labels,
                               int *count, int *default_label,
                               bool *has_default)
{
    size_t i = start;
    int depth = 0;

    *count = 0;
    *has_default = false;
    *default_label = 0;

    while (g_tokens[i].type != TOK_EOF) {
        token_type t = g_tokens[i].type;
        if (t == TOK_LBRACE) {
            depth++;
        } else if (t == TOK_RBRACE) {
            if (depth == 0) {
                return i;
            }
            depth--;
        } else if (depth == 0 && t == TOK_CASE) {
            size_t j = i + 1;
            int64_t sign = 1;
            if (g_tokens[j].type == TOK_MINUS) {
                sign = -1;
                j++;
            } else if (g_tokens[j].type == TOK_PLUS) {
                j++;
            }
            if (g_tokens[j].type != TOK_NUM) {
                fprintf(stderr,
                        "k3cc:%d: error: case label must be a constant "
                        "integer\n",
                        g_tokens[j].line);
                exit(1);
            }
            if (*count >= MAX_SWITCH_CASES) {
                fprintf(stderr,
                        "k3cc: error: too many case labels (maximum %d)\n",
                        MAX_SWITCH_CASES);
                exit(1);
            }
            int64_t value = sign * g_tokens[j].val;
            if (value < -9841 || value > 9841) {
                fprintf(stderr,
                        "k3cc:%d: error: case label %lld does not fit in a "
                        "9-trit word [-9841..9841]\n",
                        g_tokens[j].line, (long long)value);
                exit(1);
            }
            values[*count] = value;
            labels[*count] = ++g_label_counter;
            (*count)++;
            i = j; /* continue after the constant */
        } else if (depth == 0 && t == TOK_DEFAULT) {
            if (*has_default) {
                fprintf(stderr, "k3cc:%d: error: duplicate default label\n",
                        g_tokens[i].line);
                exit(1);
            }
            *has_default = true;
            *default_label = ++g_label_counter;
        }
        i++;
    }

    fprintf(stderr, "k3cc: error: unterminated switch body\n");
    exit(1);
}

/* switch (expr) { case c: ... default: ... } with C fallthrough.  The
 * controlling value stays in r0 while the dispatch chain compares it against
 * each case constant.  Cases must sit directly in the switch body (not inside
 * a nested block), and `break` uses the loop-frame stack with a -1 continue
 * label so `continue` still finds the enclosing loop. */
static void parse_switch(FILE *out)
{
    int64_t values[MAX_SWITCH_CASES];
    int labels[MAX_SWITCH_CASES];
    int count = 0;
    int default_label = 0;
    bool has_default = false;

    expect_tok(TOK_LPAREN, "'(' after switch");
    parse_expression(out);
    expect_tok(TOK_RPAREN, "')' after switch expression");
    expect_tok(TOK_LBRACE, "'{' after switch");

    size_t body_start = g_token_pos;
    size_t body_end = scan_switch_body(body_start, values, labels, &count,
                                       &default_label, &has_default);

    int end_label = ++g_label_counter;

    /* Dispatch: r0 holds the controlling value. */
    for (int k = 0; k < count; ++k) {
        fprintf(out, "    li r1, #%lld\n", (long long)values[k]);
        fprintf(out, "    cmp r2, r0, r1\n");
        fprintf(out, "    brz r2, .L%d\n", labels[k]);
    }
    fprintf(out, "    jmp .L%d\n", has_default ? default_label : end_label);

    /* Bodies, with labels at the case/default markers. */
    loop_push(end_label, -1);
    g_token_pos = body_start;
    int k = 0;
    while (g_token_pos < body_end) {
        if (cur_tok()->type == TOK_CASE) {
            advance_tok();
            (void)parse_const_int("case label");
            expect_tok(TOK_COLON, "':' after case label");
            fprintf(out, ".L%d:\n", labels[k++]);
        } else if (cur_tok()->type == TOK_DEFAULT) {
            advance_tok();
            expect_tok(TOK_COLON, "':' after default");
            fprintf(out, ".L%d:\n", default_label);
        } else {
            parse_statement(out);
        }
    }
    loop_pop();
    expect_tok(TOK_RBRACE, "'}' after switch body");
    fprintf(out, ".L%d:\n", end_label);
}

static void parse_statement(FILE *out)
{
    if (match_tok(TOK_SWITCH)) {
        parse_switch(out);
    } else if (match_tok(TOK_IF)) {
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
    } else if (match_tok(TOK_DO)) {
        int lbl_body = ++g_label_counter;
        int lbl_cond = ++g_label_counter;
        int lbl_end = ++g_label_counter;

        fprintf(out, ".L%d:\n", lbl_body);
        loop_push(lbl_end, lbl_cond);
        parse_statement(out);
        loop_pop();

        fprintf(out, ".L%d:\n", lbl_cond);
        expect_tok(TOK_WHILE, "'while' after do body");
        expect_tok(TOK_LPAREN, "'(' after do-while");
        parse_expression(out);
        expect_tok(TOK_RPAREN, "')' after do-while condition");
        expect_tok(TOK_SEMI, "';' after do-while");
        emit_cond_jump(out, "brz", lbl_body); /* repeat while r0 != 0 */
        fprintf(out, ".L%d:\n", lbl_end);
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
        if (cur_tok()->type == TOK_INT || cur_tok()->type == TOK_ENUM || cur_tok()->type == TOK_STRUCT) {
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
        int frame = -1;
        for (int k = (int)g_loop_depth - 1; k >= 0; --k) {
            if (g_continue_labels[k] >= 0) {
                frame = k;
                break;
            }
        }
        if (frame < 0) {
            fprintf(stderr, "k3cc:%d: error: 'continue' outside of a loop\n",
                    cur_tok()->line);
            exit(1);
        }
        emit_frame_pop(out, g_frame_words - g_loop_locals[frame]);
        fprintf(out, "    jmp .L%d\n", g_continue_labels[frame]);
    } else if (match_tok(TOK_RETURN)) {
        if (cur_tok()->type != TOK_SEMI) {
            parse_expression(out);
        }
        expect_tok(TOK_SEMI, "';' after return");
        emit_function_return(out);
    } else if (match_tok(TOK_INT) || match_tok(TOK_ENUM) || match_tok(TOK_STRUCT)) {
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

        /* ── struct definition: struct Foo { int a; int b[4]; }; ──
         * ── or global instance: struct Foo var; ── */
        if (match_tok(TOK_STRUCT)) {
            token *sname = cur_tok();
            expect_tok(TOK_IDENT, "struct name");

            /* Global instance of an existing struct type */
            if (cur_tok()->type != TOK_LBRACE) {
                int esi = find_struct(sname->text);
                if (esi < 0) {
                    fprintf(stderr, "k3cc:%d: error: unknown struct '%s' "
                                    "(definition must appear first)\n",
                            sname->line, sname->text);
                    exit(1);
                }
                token *gvar = cur_tok();
                expect_tok(TOK_IDENT, "global struct variable name");

                int arr_count = 1;
                if (match_tok(TOK_LBRACKET)) {
                    token *cn = cur_tok();
                    if (cn->type != TOK_NUM || cn->val <= 0 ||
                        cn->val > K3_MAX_WORDS) {
                        fprintf(stderr, "k3cc:%d: error: struct array size must "
                                        "be a positive constant\n", cn->line);
                        exit(1);
                    }
                    arr_count = (int)cn->val;
                    advance_tok();
                    expect_tok(TOK_RBRACKET, "']' after struct array size");
                }
                int total = g_structs[esi].total_size * arr_count;

                if (find_global(gvar->text) >= 0) {
                    fprintf(stderr, "k3cc:%d: error: global '%s' already "
                                    "declared\n", gvar->line, gvar->text);
                    exit(1);
                }
                if (g_global_count >= MAX_VARS) {
                    fprintf(stderr, "k3cc: error: too many global variables\n");
                    exit(1);
                }
                strncpy(g_globals[g_global_count], gvar->text, MAX_IDENT - 1);
                g_globals[g_global_count][MAX_IDENT - 1] = '\0';
                g_global_values[g_global_count]  = 0;
                g_global_sizes[g_global_count]   = total;
                g_global_init_start[g_global_count]  = 0;
                g_global_init_counts[g_global_count] = 0;
                g_global_struct_idx[g_global_count]  = esi;
                g_global_count++;

                expect_tok(TOK_SEMI, "';' after global struct variable");
                continue;
            }

            if (g_struct_count >= MAX_STRUCTS) {
                fprintf(stderr, "k3cc: error: too many struct definitions\n");
                exit(1);
            }
            int si = g_struct_count++;
            strncpy(g_structs[si].name, sname->text, MAX_IDENT - 1);
            g_structs[si].name[MAX_IDENT - 1] = '\0';
            g_structs[si].field_count = 0;
            g_structs[si].total_size  = 0;

            expect_tok(TOK_LBRACE, "'{' after struct name");

            while (cur_tok()->type != TOK_RBRACE &&
                   cur_tok()->type != TOK_EOF) {
                /* Only int fields for now */
                expect_tok(TOK_INT, "field type 'int'");
                token *fname = cur_tok();
                expect_tok(TOK_IDENT, "field name");

                int fsize = 1;
                if (match_tok(TOK_LBRACKET)) {
                    token *fn = cur_tok();
                    if (fn->type != TOK_NUM || fn->val <= 0 ||
                        fn->val > K3_MAX_WORDS) {
                        fprintf(stderr,
                                "k3cc:%d: error: field array size must be "
                                "a positive constant\n", fn->line);
                        exit(1);
                    }
                    fsize = (int)fn->val;
                    advance_tok(); /* consume size */
                    expect_tok(TOK_RBRACKET, "']' after field array size");
                }

                if (g_structs[si].field_count >= MAX_FIELDS) {
                    fprintf(stderr, "k3cc: error: too many fields in struct '%s'\n",
                            g_structs[si].name);
                    exit(1);
                }
                int fi = g_structs[si].field_count++;
                strncpy(g_structs[si].fields[fi].name, fname->text, MAX_IDENT - 1);
                g_structs[si].fields[fi].name[MAX_IDENT - 1] = '\0';
                g_structs[si].fields[fi].offset = g_structs[si].total_size;
                g_structs[si].fields[fi].size   = fsize;
                g_structs[si].total_size += fsize;

                expect_tok(TOK_SEMI, "';' after field declaration");
            }
            expect_tok(TOK_RBRACE, "'}' after struct body");
            expect_tok(TOK_SEMI, "';' after struct definition");
            continue; /* next top-level item */
        }

        expect_tok(TOK_INT, "type 'int'");
        match_tok(TOK_STAR); /* allow int *func or int *global */
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
                    match_tok(TOK_STAR); /* allow int *param */
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
            g_global_struct_idx[g_global_count] = -1;
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

    /* String literals (null-terminated) for puts(). */
    if (g_string_count > 0) {
        fprintf(out, "; --- string literals ---\n");
        for (int i = 0; i < g_string_count; ++i) {
            fprintf(out, "__str_%d:\n", i);
            fprintf(out, "    .string \"%s\"\n", g_strings[i]);
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

    char basedir[1024];
    strncpy(basedir, in_path, sizeof(basedir) - 1);
    basedir[sizeof(basedir) - 1] = '\0';
    char *slash = strrchr(basedir, '/');
    if (slash) *slash = '\0'; else basedir[0] = '\0';

    lex(src, basedir);
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
