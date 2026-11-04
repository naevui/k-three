/*
 * k3dbg - scriptable debugger for K-Three ISA v0.
 *
 * Commands are read from stdin, one per line.  Prompts and the banner go to
 * stderr so that stdout remains a clean, parseable command log.  With -q the
 * banner and prompts are suppressed, which makes scripted use simple:
 *
 *     printf 'break 4\ncontinue\nregs\nquit\n' | k3dbg -q program.k3
 *
 * Exit status: 0 on normal quit/EOF, 1 on usage or load errors.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "disasm.h"
#include "image.h"
#include "isa.h"
#include "syscall.h"
#include "ternary.h"
#include "vm.h"

typedef struct debugger {
    bt_word image[K3_VM_MEMORY_WORDS];
    size_t image_count;
    bool breakpoints[K3_VM_MEMORY_WORDS];
    bool show_labels;
    unsigned long long step_limit;
    k3_vm vm;
} debugger;

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

static bool parse_address(const char *text, uint32_t *out)
{
    char *end = NULL;
    long long value;

    if (text == NULL) {
        return false;
    }
    value = strtoll(text, &end, 10);
    if (end == text || *end != '\0' ||
        value < 0 || (uint64_t)value > K3_VM_MAX_ADDRESS) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_count(const char *text, unsigned long long *out,
                        unsigned long long fallback, bool allow_zero)
{
    char *end = NULL;
    unsigned long long value;

    if (text == NULL) {
        *out = fallback;
        return true;
    }
    value = strtoull(text, &end, 10);
    if (end == text || *end != '\0' || (!allow_zero && value == 0)) {
        return false;
    }
    *out = value;
    return true;
}

static bool parse_register(const char *text, unsigned *out)
{
    const char *p = text;

    if (p == NULL || tolower((unsigned char)p[0]) != 'r' ||
        !isdigit((unsigned char)p[1])) {
        return false;
    }
    char *end = NULL;
    long value = strtol(p + 1, &end, 10);

    if (end == p + 1 || *end != '\0' ||
        value < 0 || value >= K3_VM_REGISTER_COUNT) {
        return false;
    }
    *out = (unsigned)value;
    return true;
}

static bool parse_word_value(const char *text, bt_word *out)
{
    char *end = NULL;
    long long value;

    if (text == NULL) {
        return false;
    }
    value = strtoll(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    return bt_word_from_i64((int64_t)value, out);
}

static void print_trits(const bt_word *w)
{
    char text[K3_TRITS + 1];

    for (size_t i = 0; i < K3_TRITS; ++i) {
        text[i] = w->t[i] < 0 ? '-' : (w->t[i] > 0 ? '+' : '0');
    }
    text[K3_TRITS] = '\0';
    printf("  t=[%s]", text);
}

static void print_status(const debugger *d)
{
    k3_vm_status status = k3_vm_get_status(&d->vm);
    uint32_t pc = status == K3_VM_RUNNING ? k3_vm_pc(&d->vm)
                                          : k3_vm_stop_pc(&d->vm);

    printf("status=%s pc=%u steps=%llu", status_name(status), (unsigned)pc,
           (unsigned long long)k3_vm_instruction_count(&d->vm));
    if (status == K3_VM_TRAPPED) {
        printf(" reason=%s code=%d",
               reason_name(k3_vm_trap_reason(&d->vm)),
               (int)k3_vm_trap_code(&d->vm));
    }
    printf("\n");
}

static size_t available_words(size_t address)
{
    return address + 1 < K3_VM_MEMORY_WORDS ? 2 : 1;
}

static void print_here(const debugger *d)
{
    uint32_t pc = k3_vm_pc(&d->vm);
    char text[160];

    if (pc > K3_VM_MAX_ADDRESS) {
        printf("pc=%04u  <fetch fault>\n", (unsigned)pc);
        return;
    }
    (void)k3_disasm_format(&d->vm.memory[pc], available_words(pc), 0,
                           text, sizeof(text));
    printf("pc=%04u  %s\n", (unsigned)pc, text);
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

static void disassemble(const debugger *d, uint16_t start, size_t count)
{
    bool labels[K3_VM_MEMORY_WORDS];
    size_t end = (size_t)start;

    if (count > K3_VM_MEMORY_WORDS - end) {
        end = K3_VM_MEMORY_WORDS;
    } else {
        end += count;
    }
    memset(labels, 0, sizeof(labels));
    if (d->show_labels) {
        mark_labels(d->vm.memory, K3_VM_MEMORY_WORDS, labels);
    }

    for (size_t addr = start; addr < end; ) {
        char text[160];
        size_t used;

        if (d->show_labels && labels[addr]) {
            printf("L%04zu:\n", addr);
        }
        used = k3_disasm_format(&d->vm.memory[addr], available_words(addr), 0,
                                text, sizeof(text));
        printf("%04zu: %s\n", addr, text);
        addr += used == 0 ? 1 : used;
    }
}

static void print_registers(const debugger *d)
{
    printf("pc = %u\n", (unsigned)k3_vm_pc(&d->vm));
    printf("status = %s\n", status_name(k3_vm_get_status(&d->vm)));
    for (unsigned i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        bt_word word;
        int64_t value = 0;

        (void)k3_vm_get_reg(&d->vm, (uint8_t)i, &word);
        (void)bt_word_to_i64(&word, &value);
        printf("r%u = %lld\n", i, (long long)value);
    }
}

static void print_breakpoints(const debugger *d)
{
    bool any = false;

    for (size_t i = 0; i < K3_VM_MEMORY_WORDS; ++i) {
        if (d->breakpoints[i]) {
            if (!any) {
                printf("breakpoints:");
                any = true;
            }
            printf(" %zu", i);
        }
    }
    printf("%s\n", any ? "" : "breakpoints: none");
}

static void print_memory(const debugger *d, uint32_t start, size_t count)
{
    size_t end = (size_t)start;

    if (count > K3_VM_MEMORY_WORDS - end) {
        end = K3_VM_MEMORY_WORDS;
    } else {
        end += count;
    }
    for (size_t addr = start; addr < end; ++addr) {
        bt_word word;
        int64_t value = 0;

        (void)k3_vm_get_mem(&d->vm, (uint32_t)addr, &word);
        (void)bt_word_to_i64(&word, &value);
        printf("mem[%zu] = %lld", addr, (long long)value);
        print_trits(&word);
        printf("\n");
    }
}

static void do_step(debugger *d, unsigned long long count)
{
    for (unsigned long long i = 0; i < count; ++i) {
        if (k3_vm_get_status(&d->vm) != K3_VM_RUNNING) {
            break;
        }
        (void)k3_vm_step(&d->vm);
    }

    if (k3_vm_get_status(&d->vm) != K3_VM_RUNNING) {
        print_status(d);
    } else {
        print_here(d);
    }
}

static void do_continue(debugger *d)
{
    unsigned long long steps = 0;
    bool first = true;

    if (k3_vm_get_status(&d->vm) != K3_VM_RUNNING) {
        print_status(d);
        return;
    }

    while (steps < d->step_limit &&
           k3_vm_get_status(&d->vm) == K3_VM_RUNNING) {
        uint16_t pc = k3_vm_pc(&d->vm);

        if (!first && pc < K3_VM_MEMORY_WORDS && d->breakpoints[pc]) {
            printf("breakpoint at %u\n", (unsigned)pc);
            break;
        }
        first = false;
        (void)k3_vm_step(&d->vm);
        ++steps;
    }

    if (k3_vm_get_status(&d->vm) == K3_VM_RUNNING) {
        if (steps >= d->step_limit) {
            printf("step limit reached after %llu instructions\n", steps);
        }
        print_here(d);
    } else {
        print_status(d);
    }
}

static void do_reset(debugger *d)
{
    k3_syscall_reset();
    k3_vm_callbacks callbacks = {
        .user = NULL,
        .on_step = NULL,
        .on_stop = NULL,
        .on_syscall = k3_host_syscall,
    };
    k3_vm_init(&d->vm, &callbacks);
    (void)k3_vm_load(&d->vm, d->image, d->image_count, 0);
    printf("reset\n");
    print_here(d);
}

static void print_dashboard(const debugger *d)
{
    printf("=== K-Three CPU Dashboard ===\n");
    print_status(d);
    printf("--- Registers ---\n");
    for (unsigned i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        bt_word word;
        int64_t val = 0;
        (void)k3_vm_get_reg(&d->vm, (uint8_t)i, &word);
        (void)bt_word_to_i64(&word, &val);
        char trits[K3_TRITS + 1];
        for (size_t t = 0; t < K3_TRITS; ++t) {
            trits[t] = word.t[t] < 0 ? '-' : (word.t[t] > 0 ? '+' : '0');
        }
        trits[K3_TRITS] = '\0';
        printf("  r%u = %6lld  [%s]%s", i, (long long)val, trits,
               (i % 2 == 1) ? "\n" : "   ");
    }

    bt_word sp_word;
    int64_t sp_val = 0;
    (void)k3_vm_get_reg(&d->vm, 7, &sp_word);
    (void)bt_word_to_i64(&sp_word, &sp_val);
    if (sp_val >= 0 && sp_val < K3_VM_MEMORY_WORDS) {
        printf("--- Stack (SP = r7 = %lld) ---\n", (long long)sp_val);
        for (size_t offset = 0; offset < 4 && sp_val + (int64_t)offset < K3_VM_MEMORY_WORDS; ++offset) {
            bt_word mem_w;
            int64_t mem_v = 0;
            (void)k3_vm_get_mem(&d->vm, (uint32_t)(sp_val + offset), &mem_w);
            (void)bt_word_to_i64(&mem_w, &mem_v);
            printf("  [%04lld] = %lld\n", (long long)(sp_val + offset), (long long)mem_v);
        }
    }

    printf("--- Disassembly ---\n");
    uint32_t cur_pc = k3_vm_get_status(&d->vm) == K3_VM_RUNNING ? k3_vm_pc(&d->vm) : k3_vm_stop_pc(&d->vm);
    uint32_t start_pc = (cur_pc >= 2) ? (uint32_t)(cur_pc - 2) : 0;
    for (size_t i = 0; i < 6 && (size_t)start_pc + i < d->image_count; ++i) {
        uint32_t addr = (uint32_t)(start_pc + i);
        char text[160];
        (void)k3_disasm_format(&d->vm.memory[addr], available_words(addr), 0, text, sizeof(text));
        const char *marker = (addr == cur_pc) ? "=>" : (d->breakpoints[addr] ? " *" : "  ");
        printf("%s %04u: %s\n", marker, (unsigned)addr, text);
    }
    printf("=============================\n");
}

static void print_stack(const debugger *d, size_t count)
{
    bt_word sp_word;
    int64_t sp_val = 0;
    (void)k3_vm_get_reg(&d->vm, 7, &sp_word);
    (void)bt_word_to_i64(&sp_word, &sp_val);
    if (sp_val < 0 || sp_val >= K3_VM_MEMORY_WORDS) {
        printf("stack pointer r7 (%lld) is out of memory range\n", (long long)sp_val);
        return;
    }
    printf("Stack top at %lld:\n", (long long)sp_val);
    for (size_t i = 0; i < count && sp_val + (int64_t)i < K3_VM_MEMORY_WORDS; ++i) {
        uint32_t addr = (uint32_t)(sp_val + i);
        bt_word w;
        int64_t val = 0;
        (void)k3_vm_get_mem(&d->vm, addr, &w);
        (void)bt_word_to_i64(&w, &val);
        printf("  [%04u] (SP+%zu) = %lld", (unsigned)addr, i, (long long)val);
        print_trits(&w);
        printf("\n");
    }
}

static void do_save(const debugger *d, const char *path, bool binary)
{
    bt_word words[K3_VM_MEMORY_WORDS];
    bool ok;

    if (path == NULL) {
        printf("error: usage: save PATH [text|bin]\n");
        return;
    }

    for (size_t i = 0; i < K3_VM_MEMORY_WORDS; ++i) {
        (void)k3_vm_get_mem(&d->vm, (uint16_t)i, &words[i]);
    }

    ok = binary ? k3_image_write_binary(path, words, K3_VM_MEMORY_WORDS, 0)
                : k3_image_write(path, words, K3_VM_MEMORY_WORDS);
    if (!ok) {
        printf("error: cannot write '%s'\n", path);
        return;
    }
    printf("saved %s (%u words, %s)\n", path,
           (unsigned)K3_VM_MEMORY_WORDS, binary ? "binary" : "text");
}

static void help(void)
{
    printf(
        "commands:\n"
        "  dashboard         show visual state, registers, stack and code\n"
        "  step [N]          execute N instructions (default 1)\n"
        "  continue          run until a breakpoint, halt, trap or limit\n"
        "  regs              show pc, status and all registers\n"
        "  reg rN            show one register\n"
        "  stack [N]         show N words on stack from r7\n"
        "  set rN VALUE      set a register\n"
        "  set pc ADDR       set the program counter\n"
        "  mem ADDR [N]      show N memory words (default 8)\n"
        "  setmem ADDR VALUE set a memory word\n"
        "  dis [ADDR [N]]    disassemble N words (default 8)\n"
        "  break ADDR        set a breakpoint; no argument lists them\n"
        "  delete [ADDR]     clear one breakpoint, or all of them\n"
        "  info              show status, pc, steps and breakpoints\n"
        "  reset             reload the program and clear registers\n"
        "  save PATH [bin]   dump VM memory to a text or .k3b binary image\n"
        "  quit              exit\n");
}

int main(int argc, char **argv)
{
    debugger d;
    bool quiet = false;
    const char *path = NULL;
    char line[256];

    memset(&d, 0, sizeof(d));
    d.step_limit = 1000000;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-q") == 0 ||
            strcmp(argv[i], "--quiet") == 0) {
            quiet = true;
        } else if (strcmp(argv[i], "-l") == 0 ||
                   strcmp(argv[i], "--labels") == 0) {
            d.show_labels = true;
        } else if (strcmp(argv[i], "-s") == 0 ||
                   strcmp(argv[i], "--steps") == 0) {
            char *end = NULL;

            if (i + 1 >= argc) {
                fprintf(stderr, "usage: %s [-q] [-l] [-s N] program.k3\n",
                        argv[0]);
                return 1;
            }
            d.step_limit = strtoull(argv[++i], &end, 10);
            if (end == argv[i] || *end != '\0') {
                fprintf(stderr, "%s: invalid step limit '%s'\n",
                        argv[0], argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            fprintf(stderr,
                    "usage: %s [-q] [-l] [-s N] program.k3\n"
                    "  -q, --quiet   suppress the banner and prompts\n"
                    "  -l, --labels  mark branch targets in disassembly\n"
                    "  -s, --steps N continue step limit (default 1000000)\n",
                    argv[0]);
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argv[i]);
            return 1;
        } else if (path == NULL) {
            path = argv[i];
        } else {
            fprintf(stderr, "usage: %s [-q] [-l] [-s N] program.k3\n",
                    argv[0]);
            return 1;
        }
    }

    if (path == NULL) {
        fprintf(stderr, "usage: %s [-q] [-l] [-s N] program.k3\n", argv[0]);
        return 1;
    }
    if (!k3_image_read(path, d.image, K3_VM_MEMORY_WORDS, &d.image_count)) {
        return 1;
    }
    if (d.image_count == 0) {
        fprintf(stderr, "%s: no words to load\n", path);
        return 1;
    }

    k3_vm_callbacks callbacks = {
        .user = NULL,
        .on_step = NULL,
        .on_stop = NULL,
        .on_syscall = k3_host_syscall,
    };
    k3_vm_init(&d.vm, &callbacks);
    if (!k3_vm_load(&d.vm, d.image, d.image_count, 0)) {
        fprintf(stderr, "%s: failed to load program\n", path);
        return 1;
    }

    if (!quiet) {
        fprintf(stderr, "k3dbg - K-Three ISA v0 debugger\n");
        fprintf(stderr, "loaded %s: %zu words\n", path, d.image_count);
        fprintf(stderr, "type 'help' for commands\n");
    }

    for (;;) {
        char *cmd;
        char *arg1;
        char *arg2;

        if (!quiet) {
            fprintf(stderr, "(k3dbg) ");
        }
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        cmd = strtok(line, " \t\r\n");
        if (cmd == NULL) {
            continue;
        }
        arg1 = strtok(NULL, " \t\r\n");
        arg2 = strtok(NULL, " \t\r\n");

        if (str_eq_ci(cmd, "help") || str_eq_ci(cmd, "h") ||
            str_eq_ci(cmd, "?")) {
            help();
        } else if (str_eq_ci(cmd, "dashboard") || str_eq_ci(cmd, "dash") ||
                   str_eq_ci(cmd, "view")) {
            print_dashboard(&d);
        } else if (str_eq_ci(cmd, "stack") || str_eq_ci(cmd, "stk")) {
            unsigned long long count = 8;
            if (arg1 != NULL && !parse_count(arg1, &count, 8, false)) {
                printf("error: invalid count\n");
                continue;
            }
            print_stack(&d, (size_t)count);
        } else if (str_eq_ci(cmd, "quit") || str_eq_ci(cmd, "q") ||
                   str_eq_ci(cmd, "exit")) {
            break;
        } else if (str_eq_ci(cmd, "step") || str_eq_ci(cmd, "s")) {
            unsigned long long count;

            if (!parse_count(arg1, &count, 1, true)) {
                printf("error: invalid step count\n");
                continue;
            }
            do_step(&d, count);
        } else if (str_eq_ci(cmd, "continue") || str_eq_ci(cmd, "c")) {
            do_continue(&d);
        } else if (str_eq_ci(cmd, "regs") || str_eq_ci(cmd, "r")) {
            print_registers(&d);
        } else if (str_eq_ci(cmd, "reg")) {
            unsigned reg;

            if (!parse_register(arg1, &reg)) {
                printf("error: expected a register r0..r7\n");
                continue;
            }
            bt_word word;
            int64_t value = 0;

            (void)k3_vm_get_reg(&d.vm, (uint8_t)reg, &word);
            (void)bt_word_to_i64(&word, &value);
            printf("r%u = %lld\n", reg, (long long)value);
        } else if (str_eq_ci(cmd, "set")) {
            if (arg1 == NULL || arg2 == NULL) {
                printf("error: usage: set rN VALUE | set pc ADDR\n");
                continue;
            }
            if (str_eq_ci(arg1, "pc")) {
                uint32_t pc;

                if (!parse_address(arg2, &pc)) {
                    printf("error: invalid pc\n");
                    continue;
                }
                (void)k3_vm_set_pc(&d.vm, pc);
                printf("pc = %u\n", (unsigned)pc);
            } else {
                unsigned reg;
                bt_word word;

                if (!parse_register(arg1, &reg)) {
                    printf("error: expected a register r0..r7 or pc\n");
                    continue;
                }
                if (!parse_word_value(arg2, &word)) {
                    printf("error: value must fit in 9 trits (-9841..9841)\n");
                    continue;
                }
                (void)k3_vm_set_reg(&d.vm, (uint8_t)reg, &word);
                int64_t value = 0;
                (void)bt_word_to_i64(&word, &value);
                printf("r%u = %lld\n", reg, (long long)value);
            }
        } else if (str_eq_ci(cmd, "mem")) {
            uint32_t addr;
            unsigned long long count;

            if (!parse_address(arg1, &addr)) {
                printf("error: invalid address\n");
                continue;
            }
            if (!parse_count(arg2, &count, 8, false)) {
                printf("error: invalid count\n");
                continue;
            }
            print_memory(&d, addr, (size_t)count);
        } else if (str_eq_ci(cmd, "setmem")) {
            uint32_t addr;
            bt_word word;

            if (!parse_address(arg1, &addr) || !parse_word_value(arg2, &word)) {
                printf("error: usage: setmem ADDR VALUE\n");
                continue;
            }
            (void)k3_vm_set_mem(&d.vm, addr, &word);
            int64_t value = 0;
            (void)bt_word_to_i64(&word, &value);
            printf("mem[%u] = %lld\n", (unsigned)addr, (long long)value);
        } else if (str_eq_ci(cmd, "dis") || str_eq_ci(cmd, "d")) {
            uint32_t addr = k3_vm_get_status(&d.vm) == K3_VM_RUNNING
                                ? k3_vm_pc(&d.vm)
                                : k3_vm_stop_pc(&d.vm);
            unsigned long long count;

            if (addr > K3_VM_MAX_ADDRESS) {
                addr = 0;
            }
            if (arg1 != NULL && !parse_address(arg1, &addr)) {
                printf("error: invalid address\n");
                continue;
            }
            if (!parse_count(arg2, &count, 8, false)) {
                printf("error: invalid count\n");
                continue;
            }
            disassemble(&d, addr, (size_t)count);
        } else if (str_eq_ci(cmd, "break") || str_eq_ci(cmd, "b")) {
            uint32_t addr;

            if (arg1 == NULL) {
                print_breakpoints(&d);
                continue;
            }
            if (!parse_address(arg1, &addr)) {
                printf("error: invalid address\n");
                continue;
            }
            d.breakpoints[addr] = true;
            printf("breakpoint at %u\n", (unsigned)addr);
        } else if (str_eq_ci(cmd, "delete")) {
            if (arg1 == NULL) {
                memset(d.breakpoints, 0, sizeof(d.breakpoints));
                printf("all breakpoints cleared\n");
                continue;
            }
            uint32_t addr;

            if (!parse_address(arg1, &addr)) {
                printf("error: invalid address\n");
                continue;
            }
            d.breakpoints[addr] = false;
            printf("breakpoint cleared at %u\n", (unsigned)addr);
        } else if (str_eq_ci(cmd, "info") || str_eq_ci(cmd, "i")) {
            print_status(&d);
            print_breakpoints(&d);
        } else if (str_eq_ci(cmd, "reset")) {
            do_reset(&d);
        } else if (str_eq_ci(cmd, "save")) {
            if (arg1 == NULL) {
                printf("error: usage: save PATH [text|bin]\n");
                continue;
            }
            bool is_bin = false;
            size_t len1 = strlen(arg1);
            if (len1 >= 4 && strcmp(arg1 + len1 - 4, ".k3b") == 0) {
                is_bin = true;
            }
            if (arg2 != NULL) {
                if (str_eq_ci(arg2, "bin") || str_eq_ci(arg2, "binary") ||
                    str_eq_ci(arg2, "-b")) {
                    is_bin = true;
                } else if (str_eq_ci(arg2, "text") || str_eq_ci(arg2, "txt")) {
                    is_bin = false;
                }
            }
            do_save(&d, arg1, is_bin);
        } else {
            printf("error: unknown command '%s' (try 'help')\n", cmd);
        }

    }

    return 0;
}
