/*
 * k3dis - disassembler for K-Three ISA v0 word images.
 *
 * Input format is the k3asm/k3vm format: whitespace-separated signed decimal
 * words, one or more per line, with ';', '#' and '//' comments.
 *
 * Usage: k3dis [-t] [-w] [-l] program.k3
 *   -t   also print the raw trits of every word
 *   -w   print words only, without decoding them
 *   -l   mark branch targets with synthetic Lnnnn labels
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disasm.h"
#include "image.h"
#include "isa.h"
#include "ternary.h"
#include "vm.h"

static void format_trits(const bt_word *w, char out[K3_TRITS + 1])
{
    for (size_t i = 0; i < K3_TRITS; ++i) {
        out[i] = w->t[i] < 0 ? '-' : (w->t[i] > 0 ? '+' : '0');
    }
    out[K3_TRITS] = '\0';
}

static void print_trits(const bt_word *w)
{
    char text[K3_TRITS + 1];

    format_trits(w, text);
    printf("  t=[%s]", text);
}

static bool is_branch(unsigned opcode)
{
    return opcode == K3_OP_BRZ || opcode == K3_OP_BRNZ ||
           opcode == K3_OP_BRN || opcode == K3_OP_BRP;
}

static void mark_labels(const bt_word *words, size_t count, bool *labels)
{
    for (size_t i = 0; i < count; ) {
        k3_isa_insn in;

        k3_isa_decode(&words[i], &in);
        if (is_branch(in.opcode)) {
            int64_t target = (int64_t)i + 1 + (int64_t)in.imm;

            if (target >= 0 && target < (int64_t)count) {
                labels[(size_t)target] = true;
            }
        }
        i += (in.opcode == K3_OP_LI && i + 1 < count) ? 2 : 1;
    }
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-t] [-w] [-l] program.k3\n"
            "  -t, --trits   show the raw trits of every word\n"
            "  -w, --words   do not decode instructions\n"
            "  -l, --labels  mark branch targets with Lnnnn labels\n",
            prog);
}

int main(int argc, char **argv)
{
    bool show_trits = false;
    bool words_only = false;
    bool show_labels = false;
    const char *path = NULL;
    bt_word words[K3_VM_MEMORY_WORDS];
    bool labels[K3_VM_MEMORY_WORDS];
    size_t count = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-t") == 0 ||
            strcmp(argv[i], "--trits") == 0) {
            show_trits = true;
        } else if (strcmp(argv[i], "-w") == 0 ||
                   strcmp(argv[i], "--words") == 0) {
            words_only = true;
        } else if (strcmp(argv[i], "-l") == 0 ||
                   strcmp(argv[i], "--labels") == 0) {
            show_labels = true;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            usage(argv[0]);
            return 1;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (path == NULL) {
        usage(argv[0]);
        return 1;
    }

    if (!k3_image_read(path, words, K3_VM_MEMORY_WORDS, &count)) {
        return 1;
    }

    if (show_labels) {
        memset(labels, 0, sizeof(labels));
        mark_labels(words, count, labels);
    }

    for (size_t i = 0; i < count; ) {
        bt_word *w = &words[i];
        int64_t value = 0;
        size_t used;

        (void)bt_word_to_i64(w, &value);
        if (show_labels && labels[i]) {
            printf("L%04zu:\n", i);
        }
        printf("%04zu: ", i);

        if (words_only) {
            printf("%lld", (long long)value);
            if (show_trits) {
                print_trits(w);
            }
            printf("\n");
            ++i;
            continue;
        }

        char text[160];

        used = k3_disasm_format(words, count, i, text, sizeof(text));
        if (used == 0) {
            printf("(invalid)\n");
            ++i;
            continue;
        }
        printf("%s", text);
        if (show_trits) {
            print_trits(w);
            if (used == 2) {
                printf("  literal");
                print_trits(&words[i + 1]);
            }
        }
        printf("\n");
        i += used;
    }

    return 0;
}
