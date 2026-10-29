/*
 * k3forth - Forth compiler and REPL for the K-Three balanced ternary processor.
 *
 * Compiles standard Forth words directly into K-Three assembly or runs
 * them on an embedded K-Three VM with host syscall I/O.
 *
 * Supported Forth words:
 *   Stack:       DUP, DROP, SWAP, OVER, ROT
 *   Arithmetic:  +, -, *, NEGATE, 1+, 1-
 *   Compare:     =, <, >, 0=, 0<, 0>
 *   Logic:       MIN, MAX
 *   Memory:      @ (fetch), ! (store)
 *   I/O:         . (print number), EMIT (print char), CR (newline), ." string "
 *   Control:     IF ... THEN, IF ... ELSE ... THEN, BEGIN ... UNTIL
 *   Defines:     : name ... ;
 *   Comments:    ( comment ), \ comment
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "isa.h"
#include "syscall.h"
#include "ternary.h"
#include "vm.h"

#define K3_FORTH_BUF_SIZE 65536
#define K3_FORTH_MAX_WORDS 256
#define K3_FORTH_STACK_INIT 2100
#define K3_FORTH_RSTACK_INIT 1800

typedef struct forth_word {
    char name[32];
    char label[48];
} forth_word_t;

typedef struct forth_compiler {
    char defs_buf[K3_FORTH_BUF_SIZE / 2];
    size_t defs_len;
    char expr_buf[K3_FORTH_BUF_SIZE / 2];
    size_t expr_len;
    forth_word_t words[K3_FORTH_MAX_WORDS];
    int nwords;
    int label_counter;
    int if_stack[64];
    int if_sp;
    int begin_stack[64];
    int begin_sp;
    bool compiling;
    char current_word[32];
} forth_compiler_t;

static bool str_eq_ci(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void emit_asm(forth_compiler_t *fc, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char line[512];
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    char *buf = fc->compiling ? fc->defs_buf : fc->expr_buf;
    size_t *len = fc->compiling ? &fc->defs_len : &fc->expr_len;
    size_t max_size = fc->compiling ? sizeof(fc->defs_buf) : sizeof(fc->expr_buf);

    size_t l = strlen(line);
    if (*len + l + 2 < max_size) {
        memcpy(buf + *len, line, l);
        *len += l;
        buf[(*len)++] = '\n';
        buf[*len] = '\0';
    }
}

static void write_full_assembly(const forth_compiler_t *fc, FILE *out)
{
    fprintf(out, "; --- K-Three Forth Preamble ---\n");
    fprintf(out, "start:\n");
    fprintf(out, "    li r7, #%d\n", K3_FORTH_STACK_INIT);
    fprintf(out, "    li r4, #%d\n", K3_FORTH_RSTACK_INIT);
    fprintf(out, "    call main_forth\n");
    fprintf(out, "    halt\n\n");

    if (fc->defs_len > 0) {
        fprintf(out, "; --- User Definitions ---\n");
        fputs(fc->defs_buf, out);
        fprintf(out, "\n");
    }

    fprintf(out, "; --- Main Execution ---\n");
    fprintf(out, "main_forth:\n");
    fprintf(out, "    addi r4, #-1\n");
    fprintf(out, "    store r6, [r4]\n");
    if (fc->expr_len > 0) {
        fputs(fc->expr_buf, out);
    }
    fprintf(out, "    load r6, [r4]\n");
    fprintf(out, "    addi r4, #1\n");
    fprintf(out, "    ret\n");
}

static bool is_number(const char *s, long *val)
{
    char *end = NULL;
    if (s == NULL || *s == '\0') return false;
    long v = strtol(s, &end, 10);
    if (end != s && *end == '\0') {
        if (val) *val = v;
        return true;
    }
    return false;
}

static void init_compiler(forth_compiler_t *fc)
{
    memset(fc, 0, sizeof(*fc));
    fc->if_sp = 0;
    fc->begin_sp = 0;
    fc->compiling = false;
    fc->label_counter = 1;
}

static int find_forth_word(const forth_compiler_t *fc, const char *name)
{
    for (int i = fc->nwords - 1; i >= 0; --i) {
        if (str_eq_ci(fc->words[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static bool compile_token(forth_compiler_t *fc, const char *token, const char **src_ptr)
{
    long val = 0;

    if (token == NULL || *token == '\0') {
        return true;
    }

    /* Comment handling */
    if (strcmp(token, "\\") == 0) {
        /* Line comment: skip to newline */
        while (**src_ptr != '\0' && **src_ptr != '\n') {
            (*src_ptr)++;
        }
        return true;
    }
    if (strcmp(token, "(") == 0) {
        /* Block comment: skip until ')' */
        while (**src_ptr != '\0' && **src_ptr != ')') {
            (*src_ptr)++;
        }
        if (**src_ptr == ')') (*src_ptr)++;
        return true;
    }

    /* Word definition start */
    if (strcmp(token, ":") == 0) {
        while (**src_ptr != '\0' && isspace((unsigned char)**src_ptr)) {
            (*src_ptr)++;
        }
        char name[32];
        if (sscanf(*src_ptr, "%31s", name) != 1) {
            fprintf(stderr, "error: ':' requires word name\n");
            return false;
        }
        *src_ptr += strlen(name);
        fc->compiling = true;
        snprintf(fc->current_word, sizeof(fc->current_word), "%s", name);

        int id = fc->nwords++;
        snprintf(fc->words[id].name, sizeof(fc->words[id].name), "%s", name);
        snprintf(fc->words[id].label, sizeof(fc->words[id].label), "w_%s_%d", name, id);

        emit_asm(fc, "");
        emit_asm(fc, "; Definition of '%s'", name);
        emit_asm(fc, "%s:", fc->words[id].label);
        emit_asm(fc, "    addi r4, #-1");
        emit_asm(fc, "    store r6, [r4]");
        return true;
    }

    /* Word definition end */
    if (strcmp(token, ";") == 0) {
        if (!fc->compiling) {
            fprintf(stderr, "error: ';' outside of definition\n");
            return false;
        }
        emit_asm(fc, "    load r6, [r4]");
        emit_asm(fc, "    addi r4, #1");
        emit_asm(fc, "    ret");
        fc->compiling = false;
        return true;
    }

    /* Numbers */
    if (is_number(token, &val)) {
        emit_asm(fc, "    li r0, #%ld", val);
        emit_asm(fc, "    push r0");
        return true;
    }

    /* Stack manipulation */
    if (str_eq_ci(token, "DUP")) {
        emit_asm(fc, "    load r0, [r7]");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (str_eq_ci(token, "DROP")) {
        emit_asm(fc, "    addi r7, #1");
        return true;
    }
    if (str_eq_ci(token, "SWAP")) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    push r0");
        emit_asm(fc, "    push r1");
        return true;
    }
    if (str_eq_ci(token, "OVER")) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    load r1, [r7]");
        emit_asm(fc, "    push r0");
        emit_asm(fc, "    push r1");
        return true;
    }
    if (str_eq_ci(token, "ROT")) {
        emit_asm(fc, "    pop r0");  /* c */
        emit_asm(fc, "    pop r1");  /* b */
        emit_asm(fc, "    pop r2");  /* a */
        emit_asm(fc, "    push r1"); /* b */
        emit_asm(fc, "    push r0"); /* c */
        emit_asm(fc, "    push r2"); /* a */
        return true;
    }

    /* Arithmetic */
    if (strcmp(token, "+") == 0) {
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    add r0, r0, r1");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "-") == 0) {
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    sub r0, r0, r1");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "*") == 0) {
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    mul r0, r0, r1");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (str_eq_ci(token, "NEGATE")) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    neg r0, r0");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "1+") == 0) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    inc r0");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "1-") == 0) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    dec r0");
        emit_asm(fc, "    push r0");
        return true;
    }

    /* Comparison */
    if (strcmp(token, "=") == 0) {
        int lbl_true = fc->label_counter++;
        int lbl_done = fc->label_counter++;
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    cmp r2, r0, r1");
        emit_asm(fc, "    brz r2, f_true_%d", lbl_true);
        emit_asm(fc, "    ldi r0, #0");
        emit_asm(fc, "    brz r0, f_done_%d", lbl_done);
        emit_asm(fc, "f_true_%d:", lbl_true);
        emit_asm(fc, "    ldi r0, #1");
        emit_asm(fc, "f_done_%d:", lbl_done);
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "<") == 0) {
        int lbl_true = fc->label_counter++;
        int lbl_done = fc->label_counter++;
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    cmp r2, r0, r1");
        emit_asm(fc, "    brn r2, f_true_%d", lbl_true);
        emit_asm(fc, "    ldi r0, #0");
        emit_asm(fc, "    brz r0, f_done_%d", lbl_done);
        emit_asm(fc, "f_true_%d:", lbl_true);
        emit_asm(fc, "    ldi r0, #1");
        emit_asm(fc, "f_done_%d:", lbl_done);
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, ">") == 0) {
        int lbl_true = fc->label_counter++;
        int lbl_done = fc->label_counter++;
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    cmp r2, r0, r1");
        emit_asm(fc, "    brp r2, f_true_%d", lbl_true);
        emit_asm(fc, "    ldi r0, #0");
        emit_asm(fc, "    brz r0, f_done_%d", lbl_done);
        emit_asm(fc, "f_true_%d:", lbl_true);
        emit_asm(fc, "    ldi r0, #1");
        emit_asm(fc, "f_done_%d:", lbl_done);
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "0=") == 0) {
        int lbl_true = fc->label_counter++;
        int lbl_done = fc->label_counter++;
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    brz r0, f_true_%d", lbl_true);
        emit_asm(fc, "    ldi r0, #0");
        emit_asm(fc, "    brz r0, f_done_%d", lbl_done);
        emit_asm(fc, "f_true_%d:", lbl_true);
        emit_asm(fc, "    ldi r0, #1");
        emit_asm(fc, "f_done_%d:", lbl_done);
        emit_asm(fc, "    push r0");
        return true;
    }

    /* Logic: MIN & MAX */
    if (str_eq_ci(token, "MIN")) {
        int lbl_take_b = fc->label_counter++;
        int lbl_done = fc->label_counter++;
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    cmp r2, r0, r1");
        emit_asm(fc, "    brp r2, f_min_b_%d", lbl_take_b);
        emit_asm(fc, "    push r0");
        emit_asm(fc, "    ldi r3, #0");
        emit_asm(fc, "    brz r3, f_min_done_%d", lbl_done);
        emit_asm(fc, "f_min_b_%d:", lbl_take_b);
        emit_asm(fc, "    push r1");
        emit_asm(fc, "f_min_done_%d:", lbl_done);
        return true;
    }
    if (str_eq_ci(token, "MAX")) {
        int lbl_take_b = fc->label_counter++;
        int lbl_done = fc->label_counter++;
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    cmp r2, r0, r1");
        emit_asm(fc, "    brn r2, f_max_b_%d", lbl_take_b);
        emit_asm(fc, "    push r0");
        emit_asm(fc, "    ldi r3, #0");
        emit_asm(fc, "    brz r3, f_max_done_%d", lbl_done);
        emit_asm(fc, "f_max_b_%d:", lbl_take_b);
        emit_asm(fc, "    push r1");
        emit_asm(fc, "f_max_done_%d:", lbl_done);
        return true;
    }

    /* Memory: @ and ! */
    if (strcmp(token, "@") == 0) {
        emit_asm(fc, "    pop r1");
        emit_asm(fc, "    load r0, [r1]");
        emit_asm(fc, "    push r0");
        return true;
    }
    if (strcmp(token, "!") == 0) {
        emit_asm(fc, "    pop r1"); /* addr */
        emit_asm(fc, "    pop r0"); /* val */
        emit_asm(fc, "    store r0, [r1]");
        return true;
    }

    /* I/O words */
    if (strcmp(token, ".") == 0) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    trap #3");  /* SYS_PUTNUM */
        emit_asm(fc, "    ldi r0, #' '");
        emit_asm(fc, "    trap #1");  /* SYS_PUTC */
        return true;
    }
    if (str_eq_ci(token, "EMIT")) {
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    trap #1");  /* SYS_PUTC */
        return true;
    }
    if (str_eq_ci(token, "CR")) {
        emit_asm(fc, "    ldi r0, #10");
        emit_asm(fc, "    trap #1");
        return true;
    }
    if (strcmp(token, ".\"") == 0) {
        /* String literal until closing quote */
        char str[256];
        size_t slen = 0;
        while (**src_ptr != '\0' && **src_ptr != '"') {
            if (slen + 1 < sizeof(str)) {
                str[slen++] = *(*src_ptr)++;
            } else {
                (*src_ptr)++;
            }
        }
        str[slen] = '\0';
        if (**src_ptr == '"') (*src_ptr)++;

        int lbl_str = fc->label_counter++;
        int lbl_after = fc->label_counter++;

        emit_asm(fc, "    ldi r3, #0");
        emit_asm(fc, "    brz r3, f_after_str_%d", lbl_after);
        emit_asm(fc, "f_str_%d:", lbl_str);
        emit_asm(fc, "    .string \"%s\"", str);
        emit_asm(fc, "f_after_str_%d:", lbl_after);
        emit_asm(fc, "    li r0, f_str_%d", lbl_str);
        emit_asm(fc, "    trap #4");  /* SYS_PUTS */
        return true;
    }

    /* Control flow: IF ... ELSE ... THEN */
    if (str_eq_ci(token, "IF")) {
        int id = fc->label_counter++;
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    brz r0, f_else_%d", id);
        fc->if_stack[fc->if_sp++] = id;
        return true;
    }
    if (str_eq_ci(token, "ELSE")) {
        if (fc->if_sp == 0) {
            fprintf(stderr, "error: ELSE without IF\n");
            return false;
        }
        int id = fc->if_stack[fc->if_sp - 1];
        int next_id = fc->label_counter++;
        emit_asm(fc, "    ldi r3, #0");
        emit_asm(fc, "    brz r3, f_then_%d", next_id);
        emit_asm(fc, "f_else_%d:", id);
        fc->if_stack[fc->if_sp - 1] = next_id;
        return true;
    }
    if (str_eq_ci(token, "THEN")) {
        if (fc->if_sp == 0) {
            fprintf(stderr, "error: THEN without IF\n");
            return false;
        }
        int id = fc->if_stack[--fc->if_sp];
        emit_asm(fc, "f_else_%d:", id);
        emit_asm(fc, "f_then_%d:", id);
        return true;
    }

    /* Control flow: BEGIN ... UNTIL */
    if (str_eq_ci(token, "BEGIN")) {
        int id = fc->label_counter++;
        emit_asm(fc, "f_begin_%d:", id);
        fc->begin_stack[fc->begin_sp++] = id;
        return true;
    }
    if (str_eq_ci(token, "UNTIL")) {
        if (fc->begin_sp == 0) {
            fprintf(stderr, "error: UNTIL without BEGIN\n");
            return false;
        }
        int id = fc->begin_stack[--fc->begin_sp];
        emit_asm(fc, "    pop r0");
        emit_asm(fc, "    brz r0, f_begin_%d", id);
        return true;
    }

    /* User-defined word call */
    int wid = find_forth_word(fc, token);
    if (wid >= 0) {
        emit_asm(fc, "    call %s", fc->words[wid].label);
        return true;
    }

    fprintf(stderr, "error: unknown Forth word '%s'\n", token);
    return false;
}

static bool compile_forth_source(forth_compiler_t *fc, const char *source)
{
    const char *p = source;
    char token[64];

    while (*p != '\0') {
        while (*p != '\0' && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') break;

        size_t len = 0;
        while (*p != '\0' && !isspace((unsigned char)*p) && len + 1 < sizeof(token)) {
            token[len++] = *p++;
        }
        token[len] = '\0';

        if (!compile_token(fc, token, &p)) {
            return false;
        }
    }

    return true;
}

static void run_repl(void)
{
    char line[512];
    printf("=== K-Three Forth REPL ===\n");
    printf("Type Forth expressions, ': name ... ;' or 'bye' to exit.\n");

    forth_compiler_t fc;
    init_compiler(&fc);

    while (printf("> "), fflush(stdout), fgets(line, sizeof(line), stdin) != NULL) {
        if (strncmp(line, "bye", 3) == 0 || strncmp(line, "BYE", 3) == 0) {
            break;
        }
        if (line[0] == '\n' || line[0] == '\r') {
            continue;
        }

        fc.expr_len = 0;
        fc.expr_buf[0] = '\0';

        if (!compile_forth_source(&fc, line)) {
            continue;
        }

        if (fc.expr_len == 0) {
            printf(" ok\n");
            continue;
        }

        /* Write temporary assembly and execute */
        FILE *tmp_s = fopen("/tmp/k3_forth_repl.s", "w");
        if (tmp_s == NULL) {
            fprintf(stderr, "error: cannot create temporary file\n");
            continue;
        }
        write_full_assembly(&fc, tmp_s);
        fclose(tmp_s);

        /* Assemble using k3asm */
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "build/k3asm /tmp/k3_forth_repl.s -o /tmp/k3_forth_repl.k3 >/dev/null 2>&1");
        if (system(cmd) != 0) {
            fprintf(stderr, "error: assembly failed\n");
            continue;
        }

        /* Run on VM */
        bt_word words[K3_VM_MEMORY_WORDS];
        size_t count = 0;
        if (k3_image_read("/tmp/k3_forth_repl.k3", words, K3_VM_MEMORY_WORDS, &count)) {
            k3_vm vm;
            k3_vm_callbacks callbacks = {
                .user = NULL,
                .on_step = NULL,
                .on_stop = NULL,
                .on_syscall = k3_host_syscall,
            };
            k3_vm_init(&vm, &callbacks);
            if (k3_vm_load(&vm, words, count, 0)) {
                (void)k3_vm_run(&vm, 1000000, NULL);
            }
        }
        printf(" ok\n");
    }
}

int main(int argc, char **argv)
{
    const char *input_path = NULL;
    const char *output_asm = NULL;
    bool emit_only = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_asm = argv[++i];
            emit_only = true;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: %s [input.fs] [-o output.s]\n", argv[0]);
            printf("  Without arguments, starts the interactive Forth REPL.\n");
            printf("  With input.fs, compiles and executes the Forth program.\n");
            printf("  With -o output.s, compiles to K-Three assembly.\n");
            return 0;
        } else if (argv[i][0] != '-') {
            input_path = argv[i];
        }
    }

    if (input_path == NULL) {
        run_repl();
        return 0;
    }

    FILE *f = fopen(input_path, "r");
    if (f == NULL) {
        fprintf(stderr, "cannot open '%s'\n", input_path);
        return 1;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = malloc((size_t)sz + 1);
    if (buf == NULL) {
        fclose(f);
        return 1;
    }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);

    forth_compiler_t fc;
    init_compiler(&fc);

    if (!compile_forth_source(&fc, buf)) {
        free(buf);
        return 1;
    }
    free(buf);

    if (emit_only && output_asm != NULL) {
        FILE *out = fopen(output_asm, "w");
        if (out == NULL) {
            fprintf(stderr, "cannot write '%s'\n", output_asm);
            return 1;
        }
        write_full_assembly(&fc, out);
        fclose(out);
        return 0;
    }

    /* Compile to temporary .s, assemble and run */
    char s_path[512];
    char k3_path[512];
    snprintf(s_path, sizeof(s_path), "%s.s", input_path);
    snprintf(k3_path, sizeof(k3_path), "%s.k3", input_path);

    FILE *out = fopen(s_path, "w");
    if (out == NULL) {
        fprintf(stderr, "cannot write '%s'\n", s_path);
        return 1;
    }
    write_full_assembly(&fc, out);
    fclose(out);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "build/k3asm \"%s\" -o \"%s\"", s_path, k3_path);
    if (system(cmd) != 0) {
        fprintf(stderr, "assembly failed\n");
        return 1;
    }

    bt_word words[K3_VM_MEMORY_WORDS];
    size_t count = 0;
    if (!k3_image_read(k3_path, words, K3_VM_MEMORY_WORDS, &count)) {
        return 1;
    }

    k3_vm vm;
    k3_vm_callbacks callbacks = {
        .user = NULL,
        .on_step = NULL,
        .on_stop = NULL,
        .on_syscall = k3_host_syscall,
    };
    k3_vm_init(&vm, &callbacks);
    if (!k3_vm_load(&vm, words, count, 0)) {
        return 1;
    }

    (void)k3_vm_run(&vm, 10000000, NULL);
    return 0;
}
