/*
 * k3vm - command-line loader and runner for K-Three ISA v0 programs.
 *
 * Input format is the k3asm/k3dis word-image format: whitespace-separated
 * signed decimal words, with ';', '#' and '//' comments.
 *
 * Exit status:
 *   0  program halted normally
 *   2  program trapped
 *   3  step limit reached
 *   1  usage, input or load error
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disasm.h"
#include "image.h"
#include "syscall.h"
#include "ternary.h"
#include "vm.h"

static const char *status_name(k3_vm_status status)
{
    switch (status) {
    case K3_VM_RUNNING:
        return "running";
    case K3_VM_HALTED:
        return "halted";
    case K3_VM_TRAPPED:
        return "trapped";
    default:
        return "unknown";
    }
}

static const char *reason_name(k3_trap_reason reason)
{
    switch (reason) {
    case K3_TRAP_NONE:
        return "none";
    case K3_TRAP_ILLEGAL:
        return "illegal";
    case K3_TRAP_OVERFLOW:
        return "overflow";
    case K3_TRAP_SHIFT:
        return "shift";
    case K3_TRAP_MEMORY:
        return "memory";
    case K3_TRAP_EXPLICIT:
        return "explicit";
    default:
        return "unknown";
    }
}

static void dump_registers(const k3_vm *vm)
{
    for (unsigned i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        bt_word word;
        int64_t value = 0;

        (void)k3_vm_get_reg(vm, (uint8_t)i, &word);
        (void)bt_word_to_i64(&word, &value);
        printf("r%u = %lld\n", i, (long long)value);
    }
}

static void dump_memory(const k3_vm *vm)
{
    for (size_t i = 0; i < K3_VM_MEMORY_WORDS; ++i) {
        bt_word word;
        int64_t value = 0;

        (void)k3_vm_get_mem(vm, (uint16_t)i, &word);
        (void)bt_word_to_i64(&word, &value);
        if (value != 0) {
            printf("mem[%zu] = %lld\n", i, (long long)value);
        }
    }
}

static void trace_instruction(const k3_vm *vm)
{
    uint16_t pc = k3_vm_pc(vm);
    char text[160];

    if (pc > K3_VM_MAX_ADDRESS) {
        fprintf(stderr, "pc=%04u  <fetch fault>\n", (unsigned)pc);
        return;
    }

    size_t available = 1;

    if ((size_t)pc + 1 < K3_VM_MEMORY_WORDS) {
        available = 2;
    }
    (void)k3_disasm_format(&vm->memory[pc], available, 0, text, sizeof(text));
    fprintf(stderr, "pc=%04u  %s\n", (unsigned)pc, text);
}

static k3_vm_status run_program(k3_vm *vm, uint64_t max_steps, bool trace,
                                uint64_t *executed)
{
    uint64_t before = k3_vm_instruction_count(vm);
    uint64_t steps = 0;

    if (!trace) {
        return k3_vm_run(vm, max_steps, executed);
    }

    while (steps < max_steps && k3_vm_get_status(vm) == K3_VM_RUNNING) {
        trace_instruction(vm);
        (void)k3_vm_step(vm);
        ++steps;
    }

    if (executed != NULL) {
        *executed = k3_vm_instruction_count(vm) - before;
    }
    return k3_vm_get_status(vm);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-t] [-d] [-m] [-q] [--no-io] [-s max_steps] program.k3\n"
            "  -t, --trace      print each executed instruction to stderr\n"
            "  -d, --dump       print registers after the program stops\n"
            "  -m, --memory     print non-zero memory after the program stops\n"
            "  -q, --quiet      do not print the status line\n"
            "      --no-io      disable host I/O syscalls (treat TRAP as fault)\n"
            "  -s, --steps N    step limit (default 1000000)\n",
            prog);
}

int main(int argc, char **argv)
{
    bool trace = false;
    bool dump_regs = false;
    bool dump_mem = false;
    bool quiet = false;
    bool enable_io = true;
    unsigned long long max_steps = 1000000;
    const char *path = NULL;
    bt_word words[K3_VM_MEMORY_WORDS];
    size_t count = 0;
    k3_vm vm;
    uint64_t executed = 0;
    k3_vm_status status;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (strcmp(argv[i], "-d") == 0 ||
                   strcmp(argv[i], "--dump") == 0) {
            dump_regs = true;
        } else if (strcmp(argv[i], "-m") == 0 ||
                   strcmp(argv[i], "--memory") == 0) {
            dump_mem = true;
        } else if (strcmp(argv[i], "-q") == 0 ||
                   strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "--no-io") == 0) {
            enable_io = false;
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--steps") == 0) {
            char *end = NULL;

            if (i + 1 >= argc) {
                usage(argv[0]);
                return 1;
            }
            max_steps = strtoull(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                fprintf(stderr, "%s: invalid step limit '%s'\n",
                        argv[0], argv[i]);
                return 1;
            }
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
    if (count == 0) {
        fprintf(stderr, "%s: no words to load\n", path);
        return 1;
    }

    k3_vm_callbacks callbacks = {
        .user = NULL,
        .on_step = NULL,
        .on_stop = NULL,
        .on_syscall = enable_io ? k3_host_syscall : NULL,
    };
    k3_vm_init(&vm, &callbacks);
    if (!k3_vm_load(&vm, words, count, 0)) {
        fprintf(stderr, "%s: failed to load program\n", path);
        return 1;
    }

    status = run_program(&vm, max_steps, trace, &executed);

    if (dump_regs) {
        dump_registers(&vm);
    }
    if (dump_mem) {
        dump_memory(&vm);
    }
    if (!quiet) {
        uint16_t pc = status == K3_VM_RUNNING ? k3_vm_pc(&vm)
                                              : k3_vm_stop_pc(&vm);

        printf("status=%s steps=%llu pc=%u",
               status_name(status),
               (unsigned long long)k3_vm_instruction_count(&vm),
               (unsigned)pc);
        if (status == K3_VM_TRAPPED) {
            printf(" reason=%s code=%d",
                   reason_name(k3_vm_trap_reason(&vm)),
                   (int)k3_vm_trap_code(&vm));
        }
        printf("\n");
    }

    switch (status) {
    case K3_VM_HALTED:
        return 0;
    case K3_VM_TRAPPED:
        return 2;
    default:
        return 3;
    }
}
