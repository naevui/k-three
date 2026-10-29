/*
 * k3asm - assembler for the K-Three ISA v0 (extended).
 *
 * Directives:
 *   .word N           emit one 9-trit word with value N
 *   .string "..."     emit null-terminated ASCII string words
 *   .ascii "..."      emit ASCII string words without null terminator
 *   .space N          emit N zero words (.zero N is an alias)
 *   .equ NAME, VAL    define a symbolic constant (.set is an alias)
 *   .include "path"   include another assembly source file
 *
 * Literals:
 *   #123 / -45        decimal signed integer
 *   'c' / '\n'        ASCII character literal
 *   0t+0- / t+0-      balanced-ternary radix-3 literal
 *   symbol / label    equ constant or label address (in LI/branches)
 *
 * Pseudoinstructions:
 *   PUSH rx           ADDI r7, #-1; STORE rx, [r7]
 *   POP rx            LOAD rx, [r7]; ADDI r7, #1
 *   CALL target       load return PC into r6, jump to target
 *   RET               JMP r6
 *   CLR rx            LDI rx, #0
 *   INC rx            ADDI rx, #1
 *   DEC rx            ADDI rx, #-1
 */

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "isa.h"
#include "ternary.h"

#define K3_MAX_LABELS 1024
#define K3_MAX_EQUS 512
#define K3_MAX_WORDS 8192
#define K3_MAX_OPERANDS 4
#define K3_MAX_INCLUDE_DEPTH 16

static FILE *g_list_file = NULL;

typedef struct label {
    char name[64];
    long addr;
} label_t;

typedef struct equ {
    char name[64];
    long val;
} equ_t;

typedef struct instr {
    char mnem[16];
    char ops[K3_MAX_OPERANDS][64];
    int nops;
    char raw[1024];
} instr_t;

typedef struct assembler {
    const char *path;
    label_t labels[K3_MAX_LABELS];
    int nlabels;
    equ_t equs[K3_MAX_EQUS];
    int nequs;
    bt_word words[K3_MAX_WORDS];
    long nwords;
    int errors;
} assembler_t;

static void safe_strcpy(char *dst, const char *src, size_t cap)
{
    if (cap == 0) return;
    size_t i = 0;
    while (i + 1 < cap && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static unsigned pow3u(unsigned e)
{
    unsigned p = 1;
    while (e-- > 0) {
        p *= 3u;
    }
    return p;
}

static void trim(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

    while (start < len && isspace((unsigned char)s[start])) {
        ++start;
    }
    if (start > 0) {
        memmove(s, s + start, len - start + 1);
    }
    len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

static void strip_comment(char *s)
{
    bool leading = true;
    bool in_str = false;

    for (size_t i = 0; s[i] != '\0'; ++i) {
        if (s[i] == '"' && (i == 0 || s[i - 1] != '\\')) {
            in_str = !in_str;
        }
        if (!in_str) {
            if (s[i] == ';') {
                s[i] = '\0';
                return;
            }
            if (s[i] == '/' && s[i + 1] == '/') {
                s[i] = '\0';
                return;
            }
            if (s[i] == '#' && leading) {
                s[i] = '\0';
                return;
            }
        }
        if (!isspace((unsigned char)s[i])) {
            leading = false;
        }
    }
}

static bool take_label(char *s, char *name, size_t name_size, char **rest)
{
    char *colon = strchr(s, ':');
    size_t len;

    if (colon == NULL) {
        return false;
    }
    for (char *p = s; p < colon; ++p) {
        if (isspace((unsigned char)*p)) {
            return false;
        }
    }
    len = (size_t)(colon - s);
    if (len == 0 || len >= name_size) {
        return false;
    }
    memcpy(name, s, len);
    name[len] = '\0';
    *rest = colon + 1;
    trim(*rest);
    return true;
}

static bool decode_quoted_string(const char *raw, char *out, size_t out_cap, size_t *out_len)
{
    if (raw == NULL) return false;
    const char *start = strchr(raw, '"');
    if (!start) return false;
    const char *p = start + 1;
    size_t len = 0;

    while (*p != '\0') {
        if (*p == '"') {
            if (out && len < out_cap) out[len] = '\0';
            if (out_len) *out_len = len;
            return true;
        }
        char ch = *p++;
        if (ch == '\\') {
            if (*p == '\0') return false;
            switch (*p++) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case '0': ch = 0; break;
            case 'e': ch = 27; break;
            case '\\': ch = '\\'; break;
            case '"': ch = '"'; break;
            case '\'': ch = '\''; break;
            default: ch = *(p - 1); break;
            }
        }
        if (out && len + 1 < out_cap) {
            out[len] = ch;
        }
        len++;
    }
    return false;
}

static void tokenize(char *s, instr_t *out)
{
    memset(out, 0, sizeof(*out));
    safe_strcpy(out->raw, s, sizeof(out->raw));

    /* Check for string directives */
    char copy[1024];
    safe_strcpy(copy, s, sizeof(copy));
    char *first = strtok(copy, " \t");
    if (first != NULL) {
        char upper[32];
        size_t u = 0;
        for (; first[u] != '\0' && u + 1 < sizeof(upper); ++u) {
            upper[u] = (char)toupper((unsigned char)first[u]);
        }
        upper[u] = '\0';

        if (strcmp(upper, ".STRING") == 0 || strcmp(upper, ".ASCII") == 0 ||
            strcmp(upper, ".INCLUDE") == 0) {
            safe_strcpy(out->mnem, upper, sizeof(out->mnem));
            char str_buf[1024];
            size_t str_len = 0;
            if (decode_quoted_string(s, str_buf, sizeof(str_buf), &str_len)) {
                safe_strcpy(out->ops[0], str_buf, sizeof(out->ops[0]));
                out->nops = 1;
            }
            return;
        }
    }

    const char *p = s;
    while (*p && (*p == ' ' || *p == '\t' || *p == ',')) p++;
    if (!*p) {
        return;
    }

    /* Extract mnemonic */
    size_t mlen = 0;
    while (*p && *p != ' ' && *p != '\t' && *p != ',') {
        if (mlen + 1 < sizeof(out->mnem)) {
            out->mnem[mlen++] = (char)toupper((unsigned char)*p);
        }
        p++;
    }
    out->mnem[mlen] = '\0';

    /* Extract operands (preserving quotes and single-quoted chars with spaces) */
    while (*p && out->nops < K3_MAX_OPERANDS) {
        while (*p && (*p == ' ' || *p == '\t' || *p == ',')) p++;
        if (!*p) break;

        char *dst = out->ops[out->nops];
        size_t dlen = 0;
        size_t dmax = sizeof(out->ops[0]);

        if (*p == '\'' || (*p == '#' && *(p + 1) == '\'')) {
            if (*p == '#') {
                if (dlen + 1 < dmax) dst[dlen++] = *p++;
            }
            if (*p == '\'') {
                if (dlen + 1 < dmax) dst[dlen++] = *p++;
                while (*p && *p != '\'') {
                    if (*p == '\\' && *(p + 1)) {
                        if (dlen + 1 < dmax) dst[dlen++] = *p++;
                    }
                    if (dlen + 1 < dmax) dst[dlen++] = *p++;
                }
                if (*p == '\'') {
                    if (dlen + 1 < dmax) dst[dlen++] = *p++;
                }
            }
        } else if (*p == '"') {
            if (dlen + 1 < dmax) dst[dlen++] = *p++;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p + 1)) {
                    if (dlen + 1 < dmax) dst[dlen++] = *p++;
                }
                if (dlen + 1 < dmax) dst[dlen++] = *p++;
            }
            if (*p == '"') {
                if (dlen + 1 < dmax) dst[dlen++] = *p++;
            }
        } else {
            while (*p && *p != ' ' && *p != '\t' && *p != ',') {
                if (dlen + 1 < dmax) dst[dlen++] = *p;
                p++;
            }
        }
        dst[dlen] = '\0';
        out->nops++;
    }
}

static void report(assembler_t *as, int line, const char *msg)
{
    fprintf(stderr, "%s:%d: error: %s\n", as->path, line, msg);
    as->errors++;
}

static int find_label(const assembler_t *as, const char *name)
{
    for (int i = 0; i < as->nlabels; ++i) {
        if (strcmp(as->labels[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_equ(const assembler_t *as, const char *name)
{
    for (int i = 0; i < as->nequs; ++i) {
        if (strcmp(as->equs[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool parse_register(const char *s, unsigned *out)
{
    const char *p = s;
    char *end = NULL;
    long v;

    if (s == NULL) {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '[') {
        ++p;
    }
    if (tolower((unsigned char)*p) != 'r') {
        return false;
    }
    ++p;
    if (!isdigit((unsigned char)*p)) {
        return false;
    }
    v = strtol(p, &end, 10);
    if (end == p || (*end != '\0' && *end != ']')) {
        return false;
    }
    if (v < 0 || v > 7) {
        return false;
    }
    *out = (unsigned)v;
    return true;
}

static bool parse_char_literal(const char *s, long *out)
{
    if (s == NULL) return false;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;
    if (*s != '\'') return false;
    s++;
    if (*s == '\\') {
        s++;
        switch (*s) {
        case 'n': *out = '\n'; break;
        case 't': *out = '\t'; break;
        case 'r': *out = '\r'; break;
        case '0': *out = 0; break;
        case 'e': *out = 27; break;
        case '\\': *out = '\\'; break;
        case '\'': *out = '\''; break;
        case '\"': *out = '\"'; break;
        default: *out = (unsigned char)*s; break;
        }
        s++;
    } else {
        *out = (unsigned char)*s;
        s++;
    }
    return (*s == '\'');
}

static bool parse_ternary_literal(const char *s, long *out)
{
    if (s == NULL) return false;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '#') s++;
    if (strncmp(s, "0t", 2) == 0 || strncmp(s, "0T", 2) == 0) {
        s += 2;
    } else if (*s == 't' || *s == 'T') {
        s += 1;
    } else {
        return false;
    }
    if (*s == '\0') return false;
    long val = 0;
    for (; *s != '\0'; s++) {
        val *= 3;
        if (*s == '+') val += 1;
        else if (*s == '-') val -= 1;
        else if (*s == '0') val += 0;
        else return false;
    }
    *out = val;
    return true;
}

static bool parse_integer(assembler_t *as, const char *s, long *out)
{
    const char *p = s;
    char *end = NULL;
    long v;

    if (s == NULL) {
        return false;
    }
    while (*p == ' ' || *p == '\t') {
        ++p;
    }
    if (*p == '#') {
        ++p;
    }
    if (*p == '\0') {
        return false;
    }

    if (parse_char_literal(s, out)) {
        return true;
    }
    if (parse_ternary_literal(s, out)) {
        return true;
    }

    int eq = find_equ(as, p);
    if (eq >= 0) {
        *out = as->equs[eq].val;
        return true;
    }

    v = strtol(p, &end, 10);
    if (end == p || *end != '\0') {
        return false;
    }
    *out = v;
    return true;
}

static void put_digits(trit_t *t, unsigned value, size_t width)
{
    for (size_t i = 0; i < width; ++i) {
        t[i] = (trit_t)((int)((value / pow3u((unsigned)i)) % 3u) - 1);
    }
}

static void put_register(trit_t *t, int reg)
{
    if (reg < 0) {
        t[0] = 0;
        t[1] = 0;
        return;
    }
    put_digits(t, (unsigned)reg, 2);
}

static bt_word encode(unsigned opcode, int a, int b, int c)
{
    bt_word w;

    bt_word_zero(&w);
    put_digits(&w.t[0], opcode, 3);
    put_register(&w.t[3], a);
    put_register(&w.t[5], b);
    put_register(&w.t[7], c);
    return w;
}

static bool encode_i4(bt_word *w, unsigned opcode, int a, long imm)
{
    bt_word_zero(w);
    put_digits(&w->t[0], opcode, 3);
    put_register(&w->t[3], a);
    return bt_from_i64(imm, &w->t[5], 4);
}

static bool encode_i4_none(bt_word *w, unsigned opcode, long imm)
{
    bt_word_zero(w);
    put_digits(&w->t[0], opcode, 3);
    return bt_from_i64(imm, &w->t[5], 4);
}

static bool fits_i4(long v)
{
    return v >= -40 && v <= 40;
}

static bool fits_i9(long v)
{
    return v >= K3_WORD_MIN && v <= K3_WORD_MAX;
}

static int instruction_size(assembler_t *as, const instr_t *in)
{
    const char *mnem = in->mnem;

    if (strcmp(mnem, "NOP") == 0 || strcmp(mnem, "HALT") == 0 ||
        strcmp(mnem, "MOV") == 0 || strcmp(mnem, "LDI") == 0 ||
        strcmp(mnem, "ADD") == 0 || strcmp(mnem, "ADDI") == 0 ||
        strcmp(mnem, "SUB") == 0 || strcmp(mnem, "SUBI") == 0 ||
        strcmp(mnem, "NEG") == 0 || strcmp(mnem, "MUL") == 0 ||
        strcmp(mnem, "CMP") == 0 || strcmp(mnem, "SHL") == 0 ||
        strcmp(mnem, "SHLI") == 0 || strcmp(mnem, "SHR") == 0 ||
        strcmp(mnem, "SHRI") == 0 || strcmp(mnem, "LOAD") == 0 ||
        strcmp(mnem, "STORE") == 0 ||
        strcmp(mnem, "BRZ") == 0 || strcmp(mnem, "BRNZ") == 0 ||
        strcmp(mnem, "BRN") == 0 || strcmp(mnem, "BRP") == 0 ||
        strcmp(mnem, "TRAP") == 0 || strcmp(mnem, "CMPI") == 0 ||
        strcmp(mnem, "RET") == 0 || strcmp(mnem, "CLR") == 0 ||
        strcmp(mnem, "INC") == 0 || strcmp(mnem, "DEC") == 0 ||
        strcmp(mnem, "ERET") == 0 || strcmp(mnem, "SETVEC") == 0) {
        return 1;
    }
    if (strcmp(mnem, "JMP") == 0) {
        unsigned reg = 0;
        if (in->nops > 0 && parse_register(in->ops[0], &reg)) {
            return 1;
        }
        return 3; /* LI r5, target; JMP r5 */
    }
    if (strcmp(mnem, "LI") == 0 || strcmp(mnem, "PUSH") == 0 ||
        strcmp(mnem, "POP") == 0) {
        return 2;
    }
    if (strcmp(mnem, "CALL") == 0) {
        unsigned reg = 0;
        if (in->nops > 0 && parse_register(in->ops[0], &reg)) {
            return 3; /* LI r6, pc+3; JMP rA */
        }
        return 5;     /* LI r6, pc+5; LI r5, target; JMP r5 */
    }
    if (strcmp(mnem, ".WORD") == 0) {
        return 1;
    }
    if (strcmp(mnem, ".SPACE") == 0 || strcmp(mnem, ".ZERO") == 0) {
        long count = 0;
        if (in->nops > 0 && parse_integer(as, in->ops[0], &count) && count > 0) {
            return (int)count;
        }
        return 1;
    }
    if (strcmp(mnem, ".STRING") == 0) {
        size_t len = 0;
        (void)decode_quoted_string(in->raw, NULL, 0, &len);
        return (int)len + 1; /* null terminated */
    }
    if (strcmp(mnem, ".ASCII") == 0) {
        size_t len = 0;
        (void)decode_quoted_string(in->raw, NULL, 0, &len);
        return (int)len;
    }
    if (strcmp(mnem, ".EQU") == 0 || strcmp(mnem, ".SET") == 0 ||
        strcmp(mnem, ".INCLUDE") == 0) {
        return 0;
    }
    return -1;
}

static bool need_operands(assembler_t *as, int line,
                          const instr_t *in, int count)
{
    if (in->nops != count) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "%s expects %d operand%s, got %d",
                 in->mnem, count, count == 1 ? "" : "s", in->nops);
        report(as, line, msg);
        return false;
    }
    return true;
}

static bool operand_register(assembler_t *as, int line,
                             const instr_t *in, int index, unsigned *out)
{
    if (!parse_register(in->ops[index], out)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "invalid register '%s'",
                 in->ops[index]);
        report(as, line, msg);
        return false;
    }
    return true;
}

static bool operand_i4(assembler_t *as, int line,
                       const instr_t *in, int index, long *out)
{
    if (!parse_integer(as, in->ops[index], out)) {
        char msg[128];
        snprintf(msg, sizeof(msg), "invalid immediate '%s'",
                 in->ops[index]);
        report(as, line, msg);
        return false;
    }
    if (!fits_i4(*out)) {
        char msg[128];
        snprintf(msg, sizeof(msg),
                 "immediate %ld does not fit in 4 trits (-40..40)", *out);
        report(as, line, msg);
        return false;
    }
    return true;
}

static bool operand_branch(assembler_t *as, int line,
                           const instr_t *in, int index, long pc, long *out)
{
    long value;

    if (parse_integer(as, in->ops[index], &value)) {
        *out = value;
    } else {
        int id = find_label(as, in->ops[index]);
        if (id < 0) {
            char msg[128];
            snprintf(msg, sizeof(msg), "unknown label '%s'",
                     in->ops[index]);
            report(as, line, msg);
            return false;
        }
        *out = as->labels[id].addr - (pc + 1);
    }
    if (!fits_i4(*out)) {
        char msg[160];
        snprintf(msg, sizeof(msg),
                 "branch offset %ld does not fit in 4 trits (-40..40); "
                 "use a longer jump sequence", *out);
        report(as, line, msg);
        return false;
    }
    return true;
}

static void format_trits_list(const bt_word *w, char out[K3_TRITS + 1])
{
    for (size_t i = 0; i < K3_TRITS; ++i) {
        out[i] = w->t[i] < 0 ? '-' : (w->t[i] > 0 ? '+' : '0');
    }
    out[K3_TRITS] = '\0';
}

static void emit(assembler_t *as, int line, long addr, const bt_word *w)
{
    if (as->nwords >= K3_MAX_WORDS) {
        report(as, line, "program too large");
        return;
    }
    if (g_list_file != NULL) {
        char trits[K3_TRITS + 1];
        int64_t value = 0;
        (void)bt_word_to_i64(w, &value);
        format_trits_list(w, trits);
        fprintf(g_list_file, "%04ld: %6lld  t=[%s]\n", addr, (long long)value,
                trits);
    }
    as->words[as->nwords++] = *w;
}

static void assemble_instruction(assembler_t *as, int line,
                                 instr_t *in, long pc)
{
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    long imm = 0;
    bt_word w;

    if (strcmp(in->mnem, "NOP") == 0) {
        if (!need_operands(as, line, in, 0)) return;
        w = encode(K3_OP_NOP, -1, -1, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "HALT") == 0) {
        if (!need_operands(as, line, in, 0)) return;
        w = encode(K3_OP_HALT, -1, -1, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "MOV") == 0) {
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_register(as, line, in, 1, &b)) return;
        w = encode(K3_OP_MOV, (int)a, (int)b, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "LDI") == 0) {
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_i4(as, line, in, 1, &imm)) return;
        if (!encode_i4(&w, K3_OP_LDI, (int)a, imm)) {
            report(as, line, "immediate out of range");
            return;
        }
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "ADD") == 0 || strcmp(in->mnem, "SUB") == 0 ||
               strcmp(in->mnem, "MUL") == 0 || strcmp(in->mnem, "CMP") == 0 ||
               strcmp(in->mnem, "SHL") == 0 || strcmp(in->mnem, "SHR") == 0) {
        unsigned opcode;
        if (strcmp(in->mnem, "ADD") == 0) opcode = K3_OP_ADD;
        else if (strcmp(in->mnem, "SUB") == 0) opcode = K3_OP_SUB;
        else if (strcmp(in->mnem, "MUL") == 0) opcode = K3_OP_MUL;
        else if (strcmp(in->mnem, "CMP") == 0) opcode = K3_OP_CMP;
        else if (strcmp(in->mnem, "SHL") == 0) opcode = K3_OP_SHL;
        else opcode = K3_OP_SHR;
        if (!need_operands(as, line, in, 3)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_register(as, line, in, 1, &b)) return;
        if (!operand_register(as, line, in, 2, &c)) return;
        w = encode(opcode, (int)a, (int)b, (int)c);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "ADDI") == 0 || strcmp(in->mnem, "SUBI") == 0 ||
               strcmp(in->mnem, "CMPI") == 0) {
        unsigned opcode;
        if (strcmp(in->mnem, "ADDI") == 0) opcode = K3_OP_ADDI;
        else if (strcmp(in->mnem, "SUBI") == 0) opcode = K3_OP_SUBI;
        else opcode = K3_OP_CMPI;
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_i4(as, line, in, 1, &imm)) return;
        if (!encode_i4(&w, opcode, (int)a, imm)) {
            report(as, line, "immediate out of range");
            return;
        }
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "SHLI") == 0 || strcmp(in->mnem, "SHRI") == 0) {
        unsigned opcode = strcmp(in->mnem, "SHLI") == 0 ? K3_OP_SHLI : K3_OP_SHRI;
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_i4(as, line, in, 1, &imm)) return;
        if (imm < 0 || imm > 8) {
            char msg[128];
            snprintf(msg, sizeof(msg), "shift amount %ld must be 0..8", imm);
            report(as, line, msg);
            return;
        }
        if (!encode_i4(&w, opcode, (int)a, imm)) {
            report(as, line, "immediate out of range");
            return;
        }
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "NEG") == 0) {
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_register(as, line, in, 1, &b)) return;
        w = encode(K3_OP_NEG, (int)a, (int)b, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "LOAD") == 0 || strcmp(in->mnem, "STORE") == 0) {
        unsigned opcode = strcmp(in->mnem, "LOAD") == 0 ? K3_OP_LOAD : K3_OP_STORE;
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_register(as, line, in, 1, &b)) return;
        w = encode(opcode, (int)a, (int)b, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "JMP") == 0) {
        if (!need_operands(as, line, in, 1)) return;
        if (parse_register(in->ops[0], &a)) {
            w = encode(K3_OP_JMP, (int)a, -1, -1);
            emit(as, line, pc, &w);
        } else {
            int lbl = find_label(as, in->ops[0]);
            if (lbl >= 0) {
                imm = as->labels[lbl].addr;
            } else if (!parse_integer(as, in->ops[0], &imm)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown jmp target '%s'", in->ops[0]);
                report(as, line, msg);
                return;
            }
            w = encode(K3_OP_LI, 5, -1, -1);
            emit(as, line, pc, &w);
            bt_word_zero(&w);
            (void)bt_word_from_i64(imm, &w);
            emit(as, line, pc + 1, &w);
            w = encode(K3_OP_JMP, 5, -1, -1);
            emit(as, line, pc + 2, &w);
        }
    } else if (strcmp(in->mnem, "BRZ") == 0 || strcmp(in->mnem, "BRNZ") == 0 ||
               strcmp(in->mnem, "BRN") == 0 || strcmp(in->mnem, "BRP") == 0) {
        unsigned opcode;
        if (strcmp(in->mnem, "BRZ") == 0) opcode = K3_OP_BRZ;
        else if (strcmp(in->mnem, "BRNZ") == 0) opcode = K3_OP_BRNZ;
        else if (strcmp(in->mnem, "BRN") == 0) opcode = K3_OP_BRN;
        else opcode = K3_OP_BRP;
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        if (!operand_branch(as, line, in, 1, pc, &imm)) return;
        if (!encode_i4(&w, opcode, (int)a, imm)) {
            report(as, line, "immediate out of range");
            return;
        }
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "TRAP") == 0) {
        if (!need_operands(as, line, in, 1)) return;
        if (!parse_integer(as, in->ops[0], &imm)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "invalid trap code '%s'", in->ops[0]);
            report(as, line, msg);
            return;
        }
        if (!fits_i4(imm)) {
            report(as, line, "trap code does not fit in 4 trits (-40..40)");
            return;
        }
        if (!encode_i4_none(&w, K3_OP_TRAP, imm)) {
            report(as, line, "immediate out of range");
            return;
        }
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "LI") == 0) {
        if (!need_operands(as, line, in, 2)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        int lbl = find_label(as, in->ops[1]);
        if (lbl >= 0) {
            imm = as->labels[lbl].addr;
        } else if (!parse_integer(as, in->ops[1], &imm)) {
            char msg[128];
            snprintf(msg, sizeof(msg), "invalid literal '%s'", in->ops[1]);
            report(as, line, msg);
            return;
        }
        if (!fits_i9(imm)) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "literal %ld does not fit in 9 trits (-9841..9841)", imm);
            report(as, line, msg);
            return;
        }
        w = encode(K3_OP_LI, (int)a, -1, -1);
        emit(as, line, pc, &w);
        bt_word_zero(&w);
        if (!bt_word_from_i64((int64_t)imm, &w)) {
            report(as, line, "internal: literal encoding failed");
            return;
        }
        if (g_list_file != NULL) {
            char trits[K3_TRITS + 1];
            int64_t value = 0;
            (void)bt_word_to_i64(&w, &value);
            format_trits_list(&w, trits);
            fprintf(g_list_file, "%04ld: %6lld  t=[%s]  (literal)\n",
                    as->nwords, (long long)value, trits);
        }
        if (as->nwords >= K3_MAX_WORDS) {
            report(as, line, "program too large");
            return;
        }
        as->words[as->nwords++] = w;
    } else if (strcmp(in->mnem, ".WORD") == 0) {
        if (!need_operands(as, line, in, 1)) return;
        if (!parse_integer(as, in->ops[0], &imm)) {
            int lbl = find_label(as, in->ops[0]);
            if (lbl >= 0) {
                imm = as->labels[lbl].addr;
            } else {
                char msg[128];
                snprintf(msg, sizeof(msg), "invalid word '%s'", in->ops[0]);
                report(as, line, msg);
                return;
            }
        }
        if (!fits_i9(imm)) {
            report(as, line, "word does not fit in 9 trits (-9841..9841)");
            return;
        }
        bt_word_zero(&w);
        if (!bt_word_from_i64((int64_t)imm, &w)) {
            report(as, line, "internal: word encoding failed");
            return;
        }
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, ".SPACE") == 0 || strcmp(in->mnem, ".ZERO") == 0) {
        if (!need_operands(as, line, in, 1)) return;
        if (!parse_integer(as, in->ops[0], &imm) || imm < 0) {
            report(as, line, "invalid space count");
            return;
        }
        bt_word_zero(&w);
        for (long i = 0; i < imm; ++i) {
            emit(as, line, pc + i, &w);
        }
    } else if (strcmp(in->mnem, ".STRING") == 0 || strcmp(in->mnem, ".ASCII") == 0) {
        char str_buf[1024];
        size_t len = 0;
        if (!decode_quoted_string(in->raw, str_buf, sizeof(str_buf), &len)) {
            report(as, line, "malformed string literal");
            return;
        }
        for (size_t i = 0; i < len; ++i) {
            bt_word_zero(&w);
            (void)bt_word_from_i64((int64_t)(unsigned char)str_buf[i], &w);
            emit(as, line, pc + (long)i, &w);
        }
        if (strcmp(in->mnem, ".STRING") == 0) {
            bt_word_zero(&w);
            emit(as, line, pc + (long)len, &w);
        }
    } else if (strcmp(in->mnem, "PUSH") == 0) {
        /* ADDI r7, #-1 ; STORE rx, [r7] */
        if (!need_operands(as, line, in, 1)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        encode_i4(&w, K3_OP_ADDI, 7, -1);
        emit(as, line, pc, &w);
        w = encode(K3_OP_STORE, (int)a, 7, -1);
        emit(as, line, pc + 1, &w);
    } else if (strcmp(in->mnem, "POP") == 0) {
        /* LOAD rx, [r7] ; ADDI r7, #1 */
        if (!need_operands(as, line, in, 1)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        w = encode(K3_OP_LOAD, (int)a, 7, -1);
        emit(as, line, pc, &w);
        encode_i4(&w, K3_OP_ADDI, 7, 1);
        emit(as, line, pc + 1, &w);
    } else if (strcmp(in->mnem, "CLR") == 0) {
        /* LDI rx, #0 */
        if (!need_operands(as, line, in, 1)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        encode_i4(&w, K3_OP_LDI, (int)a, 0);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "INC") == 0) {
        /* ADDI rx, #1 */
        if (!need_operands(as, line, in, 1)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        encode_i4(&w, K3_OP_ADDI, (int)a, 1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "DEC") == 0) {
        /* ADDI rx, #-1 */
        if (!need_operands(as, line, in, 1)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        encode_i4(&w, K3_OP_ADDI, (int)a, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "RET") == 0) {
        /* JMP r6 */
        if (!need_operands(as, line, in, 0)) return;
        w = encode(K3_OP_JMP, 6, -1, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "ERET") == 0) {
        if (!need_operands(as, line, in, 0)) return;
        w = encode(K3_OP_ERET, -1, -1, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "SETVEC") == 0) {
        if (!need_operands(as, line, in, 1)) return;
        if (!operand_register(as, line, in, 0, &a)) return;
        w = encode(K3_OP_SETVEC, (int)a, -1, -1);
        emit(as, line, pc, &w);
    } else if (strcmp(in->mnem, "CALL") == 0) {
        if (!need_operands(as, line, in, 1)) return;
        if (parse_register(in->ops[0], &a)) {
            /* LI r6, pc + 3 ; JMP rA */
            w = encode(K3_OP_LI, 6, -1, -1);
            emit(as, line, pc, &w);
            bt_word_zero(&w);
            (void)bt_word_from_i64(pc + 3, &w);
            emit(as, line, pc + 1, &w);
            w = encode(K3_OP_JMP, (int)a, -1, -1);
            emit(as, line, pc + 2, &w);
        } else {
            /* LI r6, pc + 5 ; LI r5, target ; JMP r5 */
            int lbl = find_label(as, in->ops[0]);
            if (lbl >= 0) {
                imm = as->labels[lbl].addr;
            } else if (!parse_integer(as, in->ops[0], &imm)) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown call target '%s'", in->ops[0]);
                report(as, line, msg);
                return;
            }
            w = encode(K3_OP_LI, 6, -1, -1);
            emit(as, line, pc, &w);
            bt_word_zero(&w);
            (void)bt_word_from_i64(pc + 5, &w);
            emit(as, line, pc + 1, &w);

            w = encode(K3_OP_LI, 5, -1, -1);
            emit(as, line, pc + 2, &w);
            bt_word_zero(&w);
            (void)bt_word_from_i64(imm, &w);
            emit(as, line, pc + 3, &w);

            w = encode(K3_OP_JMP, 5, -1, -1);
            emit(as, line, pc + 4, &w);
        }
    } else if (strcmp(in->mnem, ".EQU") == 0 || strcmp(in->mnem, ".SET") == 0) {
        /* Handled in pass loop */
    } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "unknown mnemonic '%s'", in->mnem);
        report(as, line, msg);
    }
}

static bool run_pass(assembler_t *as, const char *filepath, int pass, long *pc_out, int depth);

static bool run_pass_file(assembler_t *as, FILE *f, const char *filepath, int pass, long *pc_out, int depth)
{
    char line[1024];
    long pc = *pc_out;
    int lineno = 0;

    while (fgets(line, sizeof(line), f) != NULL) {
        char buf[1024];
        char *rest;
        char name[64];
        instr_t in;

        lineno++;

        strncpy(buf, line, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        strip_comment(buf);
        trim(buf);

        rest = buf;
        while (take_label(rest, name, sizeof(name), &rest)) {
            if (pass == 1) {
                if (find_label(as, name) >= 0) {
                    char msg[96];
                    snprintf(msg, sizeof(msg), "duplicate label '%s'", name);
                    report(as, lineno, msg);
                    continue;
                }
                if (as->nlabels >= K3_MAX_LABELS) {
                    report(as, lineno, "too many labels");
                    continue;
                }
                snprintf(as->labels[as->nlabels].name,
                         sizeof(as->labels[0].name), "%s", name);
                as->labels[as->nlabels].addr = pc;
                as->nlabels++;
            }
        }

        trim(rest);
        if (rest[0] == '\0') {
            continue;
        }

        tokenize(rest, &in);
        if (in.mnem[0] == '\0') {
            continue;
        }

        /* Check for .EQU / .SET */
        if (strcmp(in.mnem, ".EQU") == 0 || strcmp(in.mnem, ".SET") == 0) {
            if (in.nops == 2) {
                long val = 0;
                if (parse_integer(as, in.ops[1], &val)) {
                    if (pass == 1) {
                        int eq = find_equ(as, in.ops[0]);
                        if (eq < 0 && as->nequs < K3_MAX_EQUS) {
                            safe_strcpy(as->equs[as->nequs].name, in.ops[0], sizeof(as->equs[0].name));
                            as->equs[as->nequs].val = val;
                            as->nequs++;
                        }
                    }
                } else {
                    report(as, lineno, "invalid equ value");
                }
            } else {
                report(as, lineno, ".equ expects 2 operands (NAME, VALUE)");
            }
            continue;
        }

        /* Check for .INCLUDE */
        if (strcmp(in.mnem, ".INCLUDE") == 0) {
            if (in.nops == 1) {
                if (depth >= K3_MAX_INCLUDE_DEPTH) {
                    report(as, lineno, "include depth limit exceeded");
                    continue;
                }
                /* Resolve relative path from current file directory */
                char inc_path[512];
                const char *slash = strrchr(filepath, '/');
                if (slash != NULL) {
                    size_t dirlen = (size_t)(slash - filepath + 1);
                    if (dirlen + strlen(in.ops[0]) < sizeof(inc_path)) {
                        memcpy(inc_path, filepath, dirlen);
                        inc_path[dirlen] = '\0';
                        strcat(inc_path, in.ops[0]);
                    } else {
                        safe_strcpy(inc_path, in.ops[0], sizeof(inc_path));
                    }
                } else {
                    safe_strcpy(inc_path, in.ops[0], sizeof(inc_path));
                }
                run_pass(as, inc_path, pass, &pc, depth + 1);
            } else {
                report(as, lineno, ".include expects a quoted path");
            }
            continue;
        }

        int size = instruction_size(as, &in);
        if (size < 0) {
            if (pass == 1) {
                char msg[128];
                snprintf(msg, sizeof(msg), "unknown mnemonic '%s'", in.mnem);
                report(as, lineno, msg);
            }
            continue;
        }

        if (pass == 2) {
            if (g_list_file != NULL && as->errors == 0 && in.mnem[0] != '\0') {
                fprintf(g_list_file, "%04ld:            %s\n", pc, in.raw);
            }
            assemble_instruction(as, lineno, &in, pc);
        }
        pc += size;
    }

    if (ferror(f)) {
        fprintf(stderr, "%s: read error\n", filepath);
        as->errors++;
        return false;
    }
    *pc_out = pc;
    return true;
}

static bool run_pass(assembler_t *as, const char *filepath, int pass, long *pc_out, int depth)
{
    FILE *f = fopen(filepath, "r");
    if (f == NULL) {
        fprintf(stderr, "%s: cannot open file\n", filepath);
        as->errors++;
        return false;
    }
    const char *old_path = as->path;
    as->path = filepath;
    bool ok = run_pass_file(as, f, filepath, pass, pc_out, depth);
    as->path = old_path;
    fclose(f);
    return ok;
}

static void add_predefined_equ(assembler_t *as, const char *name, long val)
{
    if (as->nequs < K3_MAX_EQUS) {
        safe_strcpy(as->equs[as->nequs].name, name, sizeof(as->equs[0].name));
        as->equs[as->nequs].val = val;
        as->nequs++;
    }
}

static void init_assembler(assembler_t *as, const char *path)
{
    memset(as, 0, sizeof(*as));
    as->path = path;

    add_predefined_equ(as, "SYS_EXIT", 0);
    add_predefined_equ(as, "SYS_PUTC", 1);
    add_predefined_equ(as, "SYS_GETC", 2);
    add_predefined_equ(as, "SYS_PUTNUM", 3);
    add_predefined_equ(as, "SYS_PUTS", 4);
    add_predefined_equ(as, "SYS_GETNUM", 5);
    add_predefined_equ(as, "SYS_PUTTRITS", 6);
    add_predefined_equ(as, "SYS_GETS", 7);
    add_predefined_equ(as, "SYS_WRITE", 8);
    add_predefined_equ(as, "SYS_RANDOM", 9);
    add_predefined_equ(as, "SYS_RANDOM_TRIT", 10);
    add_predefined_equ(as, "SYS_OPEN", 11);
    add_predefined_equ(as, "SYS_CLOSE", 12);
    add_predefined_equ(as, "SYS_READ", 13);
    add_predefined_equ(as, "SYS_WRITE_FD", 14);
    add_predefined_equ(as, "SYS_SEEK", 15);
    add_predefined_equ(as, "SYS_REMOVE", 16);
    add_predefined_equ(as, "SYS_TIME", 17);
    add_predefined_equ(as, "SYS_SLEEP", 18);
    add_predefined_equ(as, "SYS_CLEAR", 19);
    add_predefined_equ(as, "SYS_SETCURSOR", 20);
    add_predefined_equ(as, "SYS_KEYHIT", 21);
    add_predefined_equ(as, "SYS_MEMSIZE", 22);
    add_predefined_equ(as, "SYS_INSN_COUNT", 23);
    add_predefined_equ(as, "SYS_PUTC_ERR", 24);
    add_predefined_equ(as, "SYS_PUTS_ERR", 25);
    add_predefined_equ(as, "SYS_EXEC", 26);
    add_predefined_equ(as, "SYS_PUTTRITS_COLOR", 27);
    add_predefined_equ(as, "SYS_HOST_INFO", 28);
    add_predefined_equ(as, "SYS_DATETIME", 29);
    add_predefined_equ(as, "SYS_HOST_METRICS", 30);
    add_predefined_equ(as, "SYS_SYSINFO", 31);
    add_predefined_equ(as, "SYS_HELP", 32);
    add_predefined_equ(as, "SYS_DRAW_VRAM", 33);
    add_predefined_equ(as, "SYS_GUI_DESKTOP", 34);
    add_predefined_equ(as, "SYS_RAW_MODE", 35);
    add_predefined_equ(as, "SYS_RENAME", 36);
    add_predefined_equ(as, "SYS_FILE_SIZE", 37);
    add_predefined_equ(as, "SYS_FILE_EXISTS", 38);
    add_predefined_equ(as, "SYS_BANK_SWITCH", 39);
}

int main(int argc, char **argv)
{
    const char *input = NULL;
    const char *output = NULL;
    const char *listing = NULL;
    bool binary_mode = false;
    assembler_t as;
    long pc = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "usage: %s [-b] [-o out.k3|out.k3b] [-l listing.lst] in.s\n",
                        argv[0]);
                return 1;
            }
            output = argv[++i];
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--binary") == 0) {
            binary_mode = true;
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr,
                        "usage: %s [-b] [-o out.k3|out.k3b] [-l listing.lst] in.s\n",
                        argv[0]);
                return 1;
            }
            listing = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            printf("usage: %s [-b] [-o out.k3|out.k3b] [-l listing.lst] in.s\n", argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            return 1;
        } else if (input == NULL) {
            input = argv[i];
        } else if (output == NULL) {
            output = argv[i];
        } else {
            fprintf(stderr, "usage: %s [-b] [-o out.k3|out.k3b] [-l listing.lst] in.s\n",
                    argv[0]);
            return 1;
        }
    }

    if (input == NULL) {
        fprintf(stderr, "usage: %s [-b] [-o out.k3|out.k3b] [-l listing.lst] in.s\n",
                argv[0]);
        return 1;
    }

    char default_output[4096];
    if (output == NULL) {
        const char *ext = binary_mode ? ".k3b" : ".k3";
        size_t ext_len = strlen(ext);
        size_t len = strlen(input);
        if (len > 2 && strcmp(input + len - 2, ".s") == 0) {
            if (len - 2 + ext_len + 1 > sizeof(default_output)) {
                fprintf(stderr, "%s: input path too long\n", argv[0]);
                return 1;
            }
            memcpy(default_output, input, len - 2);
            memcpy(default_output + len - 2, ext, ext_len + 1);
        } else {
            if (len + ext_len + 1 > sizeof(default_output)) {
                fprintf(stderr, "%s: input path too long\n", argv[0]);
                return 1;
            }
            memcpy(default_output, input, len);
            memcpy(default_output + len, ext, ext_len + 1);
        }
        output = default_output;
    }

    size_t out_len = strlen(output);
    if (out_len >= 4 && strcmp(output + out_len - 4, ".k3b") == 0) {
        binary_mode = true;
    }

    init_assembler(&as, input);

    /* Pass 1: collect labels and calculate layout */
    pc = 0;
    if (!run_pass(&as, input, 1, &pc, 0) || as.errors > 0) {
        return 1;
    }

    /* Pass 2: generate code */
    if (listing != NULL) {
        g_list_file = fopen(listing, "w");
        if (g_list_file == NULL) {
            fprintf(stderr, "%s: cannot open listing file\n", listing);
            return 1;
        }
    }

    pc = 0;
    as.nwords = 0;
    as.path = input;
    if (!run_pass(&as, input, 2, &pc, 0) || as.errors > 0) {
        if (g_list_file != NULL) {
            fclose(g_list_file);
        }
        return 1;
    }

    if (g_list_file != NULL) {
        fclose(g_list_file);
    }

    if (binary_mode) {
        if (!k3_image_write_binary(output, as.words, (size_t)as.nwords, 0)) {
            return 1;
        }
    } else {
        if (!k3_image_write(output, as.words, (size_t)as.nwords)) {
            return 1;
        }
    }

    return 0;
}
