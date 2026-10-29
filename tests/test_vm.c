#include "vm.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

enum {
    OP_NOP = 0,
    OP_HALT = 1,
    OP_MOV = 2,
    OP_LDI = 3,
    OP_ADD = 4,
    OP_ADDI = 5,
    OP_SUB = 6,
    OP_SUBI = 7,
    OP_NEG = 8,
    OP_MUL = 9,
    OP_CMP = 10,
    OP_SHL = 11,
    OP_SHLI = 12,
    OP_SHR = 13,
    OP_SHRI = 14,
    OP_LOAD = 15,
    OP_STORE = 16,
    OP_JMP = 17,
    OP_BRZ = 18,
    OP_BRNZ = 19,
    OP_BRN = 20,
    OP_BRP = 21,
    OP_TRAP = 22,
    OP_LI = 23,
    OP_CMPI = 24,
    OP_ERET = 25,
    OP_SETVEC = 26
};

static unsigned long checks;
static unsigned long failures;

static void report_failure(const char *file, int line, const char *message)
{
    ++failures;
    if (failures <= 40) {
        fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
    }
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++checks;                                                            \
        if (!(cond)) {                                                       \
            report_failure(__FILE__, __LINE__, #cond);                       \
        }                                                                    \
    } while (0)

#define CHECK_EQ_I64(got, want)                                              \
    do {                                                                     \
        int64_t got_ = (int64_t)(got);                                        \
        int64_t want_ = (int64_t)(want);                                      \
        ++checks;                                                            \
        if (got_ != want_) {                                                  \
            ++failures;                                                      \
            if (failures <= 40) {                                            \
                fprintf(stderr,                                              \
                        "FAIL %s:%d: %s == %" PRId64 ", want %" PRId64 "\n", \
                        __FILE__, __LINE__, #got, got_, want_);              \
            }                                                                \
        }                                                                    \
    } while (0)

static int64_t word_i64(const bt_word *w)
{
    int64_t value = 0;

    (void)bt_word_to_i64(w, &value);
    return value;
}

static bt_word make_word(int64_t value)
{
    bt_word w;

    if (!bt_word_from_i64(value, &w)) {
        bt_word_zero(&w);
    }
    return w;
}

static void set_base3(trit_t *t, size_t width, unsigned value)
{
    for (size_t i = 0; i < width; ++i) {
        t[i] = (trit_t)((int)(value % 3u) - 1);
        value /= 3u;
    }
}

static bt_word encode_none(unsigned opcode)
{
    bt_word w;

    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    return w;
}

static bt_word encode_a(unsigned opcode, unsigned a)
{
    bt_word w;

    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    set_base3(&w.t[3], 2, a);
    return w;
}

static bt_word encode_r2(unsigned opcode, unsigned a, unsigned b)
{
    bt_word w;

    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    set_base3(&w.t[3], 2, a);
    set_base3(&w.t[5], 2, b);
    return w;
}

static bt_word encode_r3(unsigned opcode, unsigned a, unsigned b, unsigned c)
{
    bt_word w;

    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    set_base3(&w.t[3], 2, a);
    set_base3(&w.t[5], 2, b);
    set_base3(&w.t[7], 2, c);
    return w;
}

static bt_word encode_i4(unsigned opcode, unsigned a, int32_t immediate)
{
    bt_word w;

    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    set_base3(&w.t[3], 2, a);
    (void)bt_from_i64((int64_t)immediate, &w.t[5], 4);
    return w;
}

static bt_word encode_none_i4(unsigned opcode, int32_t immediate)
{
    bt_word w;

    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    (void)bt_from_i64((int64_t)immediate, &w.t[5], 4);
    return w;
}

static void load_program(k3_vm *vm, const bt_word *program, size_t count)
{
    k3_vm_init(vm, NULL);
    CHECK(k3_vm_load(vm, program, count, 0));
}

static k3_vm_status run_to_stop(k3_vm *vm)
{
    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(vm, 100000, &executed);

    CHECK(status != K3_VM_RUNNING);
    CHECK(executed > 0);
    return status;
}

static void check_reg(const k3_vm *vm, unsigned index, int64_t want)
{
    CHECK(index < K3_VM_REGISTER_COUNT);
    CHECK(bt_valid(vm->regs[index].t, K3_TRITS));
    CHECK_EQ_I64(word_i64(&vm->regs[index]), want);
}

static void check_mem(const k3_vm *vm, uint16_t address, int64_t want)
{
    CHECK(address <= K3_VM_MAX_ADDRESS);
    CHECK(bt_valid(vm->memory[address].t, K3_TRITS));
    CHECK_EQ_I64(word_i64(&vm->memory[address]), want);
}

static void check_halt(const k3_vm *vm, uint16_t stop_pc)
{
    CHECK_EQ_I64(k3_vm_get_status(vm), K3_VM_HALTED);
    CHECK_EQ_I64(k3_vm_trap_reason(vm), K3_TRAP_NONE);
    CHECK_EQ_I64(k3_vm_trap_code(vm), 0);
    CHECK_EQ_I64(k3_vm_stop_pc(vm), stop_pc);
}

static void check_trap(const k3_vm *vm, k3_trap_reason reason,
                       int32_t code, uint16_t stop_pc)
{
    CHECK_EQ_I64(k3_vm_get_status(vm), K3_VM_TRAPPED);
    CHECK_EQ_I64(k3_vm_trap_reason(vm), reason);
    CHECK_EQ_I64(k3_vm_trap_code(vm), code);
    CHECK_EQ_I64(k3_vm_stop_pc(vm), stop_pc);
}

static void test_encoding_examples(void)
{
    static const trit_t halt_expected[K3_TRITS] =
        { 0, -1, -1, 0, 0, 0, 0, 0, 0 };
    static const trit_t add_expected[K3_TRITS] =
        { 0, 0, -1, -1, 0, 0, -1, 1, -1 };
    static const trit_t ldi_expected[K3_TRITS] =
        { -1, 0, -1, 0, -1, 0, -1, 1, 0 };
    static const trit_t brnz_expected[K3_TRITS] =
        { 0, -1, 1, 0, -1, 1, -1, 0, 0 };

    bt_word halt = encode_none(OP_HALT);
    bt_word add = encode_r3(OP_ADD, 3, 1, 2);
    bt_word ldi = encode_i4(OP_LDI, 1, 6);
    bt_word brnz = encode_i4(OP_BRNZ, 1, -2);

    for (size_t i = 0; i < K3_TRITS; ++i) {
        CHECK_EQ_I64(halt.t[i], halt_expected[i]);
        CHECK_EQ_I64(add.t[i], add_expected[i]);
        CHECK_EQ_I64(ldi.t[i], ldi_expected[i]);
        CHECK_EQ_I64(brnz.t[i], brnz_expected[i]);
    }
}

static void test_init_and_accessors(void)
{
    k3_vm vm;
    bt_word out;
    bt_word value = make_word(123);

    k3_vm_init(&vm, NULL);

    CHECK_EQ_I64(k3_vm_get_status(&vm), K3_VM_RUNNING);
    CHECK_EQ_I64(k3_vm_trap_reason(&vm), K3_TRAP_NONE);
    CHECK_EQ_I64(k3_vm_trap_code(&vm), 0);
    CHECK_EQ_I64(k3_vm_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_last_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_stop_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_instruction_count(&vm), 0);
    CHECK(vm.callbacks.user == NULL);
    CHECK(vm.callbacks.on_step == NULL);
    CHECK(vm.callbacks.on_stop == NULL);

    for (unsigned i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        check_reg(&vm, i, 0);
    }
    for (size_t i = 0; i < K3_VM_MEMORY_WORDS; ++i) {
        CHECK_EQ_I64(word_i64(&vm.memory[i]), 0);
    }

    CHECK(k3_vm_set_reg(&vm, 7, &value));
    CHECK(k3_vm_get_reg(&vm, 7, &out));
    CHECK_EQ_I64(word_i64(&out), 123);
    CHECK(!k3_vm_set_reg(&vm, 8, &value));
    CHECK(!k3_vm_get_reg(&vm, 8, &out));
    CHECK(!k3_vm_get_reg(&vm, 0, NULL));
    CHECK(!k3_vm_set_reg(&vm, 0, NULL));

    bt_word bad = value;
    bad.t[0] = 2;
    CHECK(!k3_vm_set_reg(&vm, 0, &bad));
    check_reg(&vm, 0, 0);

    CHECK(!k3_vm_get_mem(&vm, K3_VM_MEMORY_WORDS, &out));
    CHECK(!k3_vm_set_mem(&vm, K3_VM_MEMORY_WORDS, &value));
    CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &value));
    CHECK(k3_vm_get_mem(&vm, K3_VM_MAX_ADDRESS, &out));
    CHECK_EQ_I64(word_i64(&out), 123);
    CHECK(!k3_vm_set_mem(&vm, 0, &bad));

    CHECK(k3_vm_set_pc(&vm, K3_VM_MAX_ADDRESS));
    CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MAX_ADDRESS);
    CHECK(!k3_vm_set_pc(&vm, K3_VM_MEMORY_WORDS));
    CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MAX_ADDRESS);

    bt_word program[3] = {
        make_word(1), make_word(2), make_word(3)
    };
    CHECK(k3_vm_load(&vm, program, 3, K3_VM_MAX_ADDRESS - 2));
    check_mem(&vm, K3_VM_MAX_ADDRESS - 2, 1);
    check_mem(&vm, K3_VM_MAX_ADDRESS - 1, 2);
    check_mem(&vm, K3_VM_MAX_ADDRESS, 3);
    CHECK(!k3_vm_load(&vm, program, 3, K3_VM_MAX_ADDRESS - 1));

    bt_word bad_program[3] = {
        make_word(5), make_word(6), make_word(7)
    };
    bad_program[1].t[4] = 3;
    CHECK(!k3_vm_load(&vm, bad_program, 3, 10));
    check_mem(&vm, 10, 0);
    check_mem(&vm, 11, 0);
    check_mem(&vm, 12, 0);
    CHECK(!k3_vm_load(&vm, NULL, 1, 0));
    CHECK(k3_vm_load(&vm, NULL, 0, K3_VM_MEMORY_WORDS));

    CHECK(k3_vm_set_reg(&vm, 0, &value));
    CHECK(k3_vm_set_pc(&vm, 5));
    vm.instruction_count = 9;
    vm.status = K3_VM_TRAPPED;
    vm.trap_reason = K3_TRAP_MEMORY;
    vm.trap_code = 5;
    vm.stop_pc = 7;
    vm.last_pc = 6;

    k3_vm_reset(&vm);
    CHECK_EQ_I64(k3_vm_get_status(&vm), K3_VM_RUNNING);
    CHECK_EQ_I64(k3_vm_trap_reason(&vm), K3_TRAP_NONE);
    CHECK_EQ_I64(k3_vm_trap_code(&vm), 0);
    CHECK_EQ_I64(k3_vm_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_last_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_stop_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_instruction_count(&vm), 0);
    check_reg(&vm, 0, 0);
    check_mem(&vm, K3_VM_MAX_ADDRESS - 2, 1);

    k3_vm_init(&vm, NULL);
    check_mem(&vm, K3_VM_MAX_ADDRESS - 2, 0);
}

static void test_mov_ldi_neg_li(void)
{
    bt_word program[8] = {
        encode_i4(OP_LDI, 0, -40),
        encode_r2(OP_MOV, 1, 0),
        encode_r2(OP_NEG, 2, 1),
        encode_i4(OP_LI, 3, 0),
        make_word(9841),
        encode_i4(OP_LI, 4, 0),
        make_word(-9841),
        encode_none(OP_HALT)
    };
    k3_vm vm;

    load_program(&vm, program, 8);
    run_to_stop(&vm);
    check_halt(&vm, 7);
    check_reg(&vm, 0, -40);
    check_reg(&vm, 1, -40);
    check_reg(&vm, 2, 40);
    check_reg(&vm, 3, 9841);
    check_reg(&vm, 4, -9841);
    CHECK_EQ_I64(k3_vm_instruction_count(&vm), 6);
    CHECK_EQ_I64(k3_vm_last_pc(&vm), 7);
    CHECK_EQ_I64(k3_vm_pc(&vm), 8);
}

static void test_alu(void)
{
    bt_word program[12] = {
        encode_i4(OP_LDI, 0, 40),
        encode_i4(OP_LDI, 1, 2),
        encode_r3(OP_ADD, 2, 0, 1),
        encode_r3(OP_SUB, 3, 0, 1),
        encode_r3(OP_MUL, 4, 1, 1),
        encode_r3(OP_CMP, 5, 1, 0),
        encode_r3(OP_CMP, 6, 0, 1),
        encode_i4(OP_CMPI, 1, 2),
        encode_i4(OP_ADDI, 2, -40),
        encode_i4(OP_SUBI, 3, -2),
        encode_r2(OP_NEG, 7, 0),
        encode_none(OP_HALT)
    };
    k3_vm vm;

    load_program(&vm, program, 12);
    run_to_stop(&vm);
    check_halt(&vm, 11);
    check_reg(&vm, 0, 40);
    check_reg(&vm, 1, 0);
    check_reg(&vm, 2, 2);
    check_reg(&vm, 3, 40);
    check_reg(&vm, 4, 4);
    check_reg(&vm, 5, -1);
    check_reg(&vm, 6, 1);
    check_reg(&vm, 7, -40);
    CHECK_EQ_I64(k3_vm_instruction_count(&vm), 12);

    {
        bt_word compare_program[4] = {
            encode_i4(OP_LDI, 0, 0),
            encode_i4(OP_CMPI, 0, -40),
            encode_i4(OP_CMPI, 0, 40),
            encode_none(OP_HALT)
        };

        load_program(&vm, compare_program, 4);
        run_to_stop(&vm);
        check_halt(&vm, 3);
        check_reg(&vm, 0, -1);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 4);
    }
}

static void test_shifts(void)
{
    {
        bt_word program[7] = {
            encode_i4(OP_LDI, 0, 5),
            encode_i4(OP_LDI, 1, 1),
            encode_r3(OP_SHR, 2, 0, 1),
            encode_r3(OP_SHL, 3, 0, 1),
            encode_i4(OP_SHRI, 0, 1),
            encode_i4(OP_SHLI, 0, 3),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 7);
        run_to_stop(&vm);
        check_halt(&vm, 6);
        check_reg(&vm, 0, 54);
        check_reg(&vm, 1, 1);
        check_reg(&vm, 2, 2);
        check_reg(&vm, 3, 15);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 7);
    }

    {
        bt_word program[4] = {
            encode_i4(OP_LDI, 0, 5),
            encode_i4(OP_SHLI, 0, 0),
            encode_i4(OP_SHRI, 0, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_halt(&vm, 3);
        check_reg(&vm, 0, 5);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LDI, 0, 1),
            encode_i4(OP_SHLI, 0, 8),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_halt(&vm, 2);
        check_reg(&vm, 0, 6561);
    }

    {
        bt_word program[4] = {
            encode_i4(OP_LI, 0, 0),
            make_word(-5),
            encode_i4(OP_SHRI, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_halt(&vm, 3);
        check_reg(&vm, 0, -2);
    }

    {
        bt_word program[4] = {
            encode_i4(OP_LI, 0, 0),
            make_word(9841),
            encode_i4(OP_SHRI, 0, 8),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_halt(&vm, 3);
        check_reg(&vm, 0, 1);
    }
}

static void test_memory(void)
{
    {
        bt_word program[7] = {
            encode_i4(OP_LI, 0, 0),
            make_word(K3_VM_MAX_ADDRESS),
            encode_i4(OP_LI, 1, 0),
            make_word(-5),
            encode_r2(OP_STORE, 1, 0),
            encode_r2(OP_LOAD, 2, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 7);
        run_to_stop(&vm);
        check_halt(&vm, 6);
        check_mem(&vm, K3_VM_MAX_ADDRESS, -5);
        check_reg(&vm, 2, -5);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 5);
    }

    {
        bt_word program[7] = {
            encode_i4(OP_LI, 0, 0),
            make_word(K3_VM_MAX_ADDRESS),
            encode_i4(OP_LI, 1, 0),
            make_word(7),
            encode_r2(OP_STORE, 1, 0),
            encode_r2(OP_LOAD, 0, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 7);
        run_to_stop(&vm);
        check_halt(&vm, 6);
        check_mem(&vm, K3_VM_MAX_ADDRESS, 7);
        check_reg(&vm, 0, 7);
    }

    {
        bt_word program[4] = {
            encode_i4(OP_LI, 0, 0),
            make_word(K3_VM_MAX_ADDRESS),
            encode_r2(OP_STORE, 0, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_halt(&vm, 3);
        check_mem(&vm, K3_VM_MAX_ADDRESS, K3_VM_MAX_ADDRESS);
        check_reg(&vm, 0, K3_VM_MAX_ADDRESS);
    }

    {
        bt_word program[5] = {
            encode_i4(OP_LI, 0, 0),
            make_word(K3_VM_MEMORY_WORDS),
            encode_i4(OP_LDI, 1, 7),
            encode_r2(OP_LOAD, 1, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 5);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_MEMORY, 0, 3);
        check_reg(&vm, 0, K3_VM_MEMORY_WORDS);
        check_reg(&vm, 1, 7);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 3);
    }

    {
        bt_word program[5] = {
            encode_i4(OP_LI, 0, 0),
            make_word(-1),
            encode_i4(OP_LI, 1, 0),
            make_word(7),
            encode_r2(OP_STORE, 1, 0)
        };
        k3_vm vm;
        bt_word sentinel = make_word(123);

        load_program(&vm, program, 5);
        CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &sentinel));
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_MEMORY, 0, 4);
        check_mem(&vm, K3_VM_MAX_ADDRESS, 123);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 3);
    }

    {
        bt_word halt_word = encode_none(OP_HALT);
        int64_t halt_value = word_i64(&halt_word);
        bt_word program[7] = {
            encode_i4(OP_LDI, 0, 4),
            encode_i4(OP_LI, 1, 0),
            make_word(halt_value),
            encode_r2(OP_STORE, 1, 0),
            encode_none(OP_NOP),
            encode_i4(OP_LDI, 2, 7),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 7);
        run_to_stop(&vm);
        check_halt(&vm, 4);
        check_mem(&vm, 4, halt_value);
        check_reg(&vm, 2, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 4);
    }
}

static void test_branch_case(unsigned opcode, int64_t value, bool taken)
{
    bt_word program[4] = {
        encode_i4(OP_LDI, 0, (int32_t)value),
        encode_i4(opcode, 0, 1),
        encode_i4(OP_LDI, 1, 9),
        encode_none(OP_HALT)
    };
    k3_vm vm;

    load_program(&vm, program, 4);
    run_to_stop(&vm);
    check_halt(&vm, 3);
    check_reg(&vm, 0, value);
    check_reg(&vm, 1, taken ? 0 : 9);
}

static void test_control_flow(void)
{
    {
        bt_word program[4] = {
            encode_i4(OP_LDI, 0, 3),
            encode_i4(OP_ADDI, 0, -1),
            encode_i4(OP_BRNZ, 0, -2),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_halt(&vm, 3);
        check_reg(&vm, 0, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 8);
    }

    test_branch_case(OP_BRZ, 0, true);
    test_branch_case(OP_BRZ, 1, false);
    test_branch_case(OP_BRNZ, 0, false);
    test_branch_case(OP_BRNZ, 1, true);
    test_branch_case(OP_BRN, -1, true);
    test_branch_case(OP_BRN, 0, false);
    test_branch_case(OP_BRP, 1, true);
    test_branch_case(OP_BRP, 0, false);

    {
        bt_word program[3] = {
            encode_i4(OP_LDI, 0, 0),
            encode_i4(OP_BRZ, 0, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_halt(&vm, 2);
        check_reg(&vm, 0, 0);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LDI, 0, 0),
            encode_i4(OP_BRZ, 0, -40),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_MEMORY, 0, 1);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LDI, 0, 1),
            encode_i4(OP_BRZ, 0, -40),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_halt(&vm, 2);
        check_reg(&vm, 0, 1);
    }

    {
        bt_word program[5] = {
            encode_i4(OP_LI, 0, 0),
            make_word(3),
            encode_a(OP_JMP, 0),
            encode_i4(OP_LDI, 1, 9),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 5);
        run_to_stop(&vm);
        check_halt(&vm, 4);
        check_reg(&vm, 0, 3);
        check_reg(&vm, 1, 9);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 4);
    }

    {
        bt_word program[4] = {
            encode_i4(OP_LI, 0, 0),
            make_word(K3_VM_MEMORY_WORDS),
            encode_a(OP_JMP, 0),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_MEMORY, 0, 2);
        check_reg(&vm, 0, K3_VM_MEMORY_WORDS);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }
}

static void test_overflow_traps(void)
{
    {
        bt_word program[6] = {
            encode_i4(OP_LI, 0, 0),
            make_word(9841),
            encode_i4(OP_LDI, 1, 1),
            encode_i4(OP_LDI, 2, 7),
            encode_r3(OP_ADD, 2, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 6);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 4);
        check_reg(&vm, 2, 7);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 4);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LI, 0, 0),
            make_word(9841),
            encode_i4(OP_ADDI, 0, 1)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 2);
        check_reg(&vm, 0, 9841);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }

    {
        bt_word program[5] = {
            encode_i4(OP_LI, 0, 0),
            make_word(-9841),
            encode_i4(OP_LDI, 1, 1),
            encode_r3(OP_SUB, 2, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 5);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 3);
        check_reg(&vm, 2, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 3);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LI, 0, 0),
            make_word(-9841),
            encode_i4(OP_SUBI, 0, 1)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 2);
        check_reg(&vm, 0, -9841);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }

    {
        bt_word program[7] = {
            encode_i4(OP_LI, 0, 0),
            make_word(100),
            encode_i4(OP_LI, 1, 0),
            make_word(100),
            encode_i4(OP_LDI, 2, 7),
            encode_r3(OP_MUL, 2, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 7);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 5);
        check_reg(&vm, 2, 7);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 4);
    }

    {
        bt_word program[5] = {
            encode_i4(OP_LI, 0, 0),
            make_word(9841),
            encode_i4(OP_LDI, 1, 1),
            encode_r3(OP_SHL, 2, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 5);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 3);
        check_reg(&vm, 2, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 3);
    }
}

static void test_shift_and_illegal_traps(void)
{
    {
        bt_word program[4] = {
            encode_i4(OP_LDI, 0, 5),
            encode_i4(OP_LDI, 1, 9),
            encode_r3(OP_SHL, 2, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_SHIFT, 0, 2);
        check_reg(&vm, 2, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 3);
    }

    {
        bt_word program[4] = {
            encode_i4(OP_LDI, 0, 5),
            encode_i4(OP_LDI, 1, -1),
            encode_r3(OP_SHR, 2, 0, 1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 4);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_SHIFT, 0, 2);
        check_reg(&vm, 2, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 3);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LDI, 0, 5),
            encode_i4(OP_SHLI, 0, 9),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_SHIFT, 0, 1);
        check_reg(&vm, 0, 5);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LDI, 0, 5),
            encode_i4(OP_SHRI, 0, -1),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_SHIFT, 0, 1);
        check_reg(&vm, 0, 5);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }

    {
        bt_word program[3] = {
            encode_i4(OP_LI, 0, 0),
            make_word(9841),
            encode_i4(OP_SHLI, 0, 1)
        };
        k3_vm vm;

        load_program(&vm, program, 3);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_OVERFLOW, 0, 2);
        check_reg(&vm, 0, 9841);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 2);
    }

    /* Privileged instructions in user mode trap with ILLEGAL. */
    for (unsigned opcode = 25; opcode <= 26; ++opcode) {
        bt_word program[2] = {
            encode_none(opcode),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 2);
        k3_vm_set_mode(&vm, K3_MODE_USER);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_ILLEGAL, 0, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
        CHECK_EQ_I64(k3_vm_pc(&vm), 1);
        CHECK_EQ_I64(k3_vm_last_pc(&vm), 0);
    }

    {
        bt_word program[2] = {
            encode_r3(OP_ADD, 0, 0, 8),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 2);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_ILLEGAL, 0, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
    }

    {
        bt_word mov = encode_r2(OP_MOV, 1, 2);

        set_base3(&mov.t[7], 2, 8);

        bt_word program[2] = { mov, encode_none(OP_HALT) };
        bt_word five = make_word(5);
        k3_vm vm;

        load_program(&vm, program, 2);
        check_reg(&vm, 2, 0);
        CHECK(k3_vm_set_reg(&vm, 2, &five));
        run_to_stop(&vm);
        check_halt(&vm, 1);
        check_reg(&vm, 1, 5);
    }

    {
        bt_word program[2] = {
            encode_none_i4(OP_TRAP, 7),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 2);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_EXPLICIT, 7, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
    }

    {
        bt_word program[2] = {
            encode_none_i4(OP_TRAP, -40),
            encode_none(OP_HALT)
        };
        k3_vm vm;

        load_program(&vm, program, 2);
        run_to_stop(&vm);
        check_trap(&vm, K3_TRAP_EXPLICIT, -40, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
    }
}

static void test_fetch_faults(void)
{
    {
        k3_vm vm;
        bt_word halt = encode_none(OP_HALT);

        k3_vm_init(&vm, NULL);
        CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &halt));
        CHECK(k3_vm_set_pc(&vm, K3_VM_MAX_ADDRESS));

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_HALTED);
        check_halt(&vm, K3_VM_MAX_ADDRESS);
        CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MEMORY_WORDS);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_HALTED);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
    }

    {
        k3_vm vm;
        bt_word li = encode_i4(OP_LI, 0, 0);
        bt_word literal = make_word(42);

        k3_vm_init(&vm, NULL);
        CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS - 1, &li));
        CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &literal));
        CHECK(k3_vm_set_pc(&vm, K3_VM_MAX_ADDRESS - 1));

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_RUNNING);
        check_reg(&vm, 0, 42);
        CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MEMORY_WORDS);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_TRAPPED);
        check_trap(&vm, K3_TRAP_MEMORY, 0, K3_VM_MEMORY_WORDS);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
        CHECK_EQ_I64(k3_vm_last_pc(&vm), K3_VM_MAX_ADDRESS - 1);
    }

    {
        k3_vm vm;
        bt_word nop = encode_none(OP_NOP);

        k3_vm_init(&vm, NULL);
        CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &nop));
        CHECK(k3_vm_set_pc(&vm, K3_VM_MAX_ADDRESS));

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_RUNNING);
        CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MEMORY_WORDS);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
        CHECK_EQ_I64(k3_vm_last_pc(&vm), K3_VM_MAX_ADDRESS);

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_TRAPPED);
        check_trap(&vm, K3_TRAP_MEMORY, 0, K3_VM_MEMORY_WORDS);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
        CHECK_EQ_I64(k3_vm_last_pc(&vm), K3_VM_MAX_ADDRESS);
        CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MEMORY_WORDS);
    }

    {
        k3_vm vm;
        bt_word li = encode_i4(OP_LI, 0, 0);
        bt_word value = make_word(42);

        k3_vm_init(&vm, NULL);
        CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &li));
        CHECK(k3_vm_set_reg(&vm, 0, &value));
        CHECK(k3_vm_set_pc(&vm, K3_VM_MAX_ADDRESS));

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_TRAPPED);
        check_trap(&vm, K3_TRAP_MEMORY, 0, K3_VM_MAX_ADDRESS);
        check_reg(&vm, 0, 42);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
        CHECK_EQ_I64(k3_vm_pc(&vm), K3_VM_MEMORY_WORDS);
    }

    {
        k3_vm vm;

        k3_vm_init(&vm, NULL);
        vm.memory[0].t[0] = 2;

        CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_TRAPPED);
        check_trap(&vm, K3_TRAP_ILLEGAL, 0, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 1);
    }
}

typedef struct callback_state {
    unsigned steps;
    unsigned stops;
    unsigned last_event; /* 0 = none, 1 = step, 2 = stop */
    k3_vm_status last_status;
    k3_trap_reason last_reason;
    uint16_t last_stop_pc;
    uint64_t last_count;
} callback_state;

static void record_step(void *user, const k3_vm *vm)
{
    callback_state *state = user;

    ++state->steps;
    state->last_event = 1;
    state->last_status = k3_vm_get_status(vm);
    state->last_count = k3_vm_instruction_count(vm);
}

static void record_stop(void *user, const k3_vm *vm)
{
    callback_state *state = user;

    ++state->stops;
    state->last_event = 2;
    state->last_status = k3_vm_get_status(vm);
    state->last_reason = k3_vm_trap_reason(vm);
    state->last_stop_pc = k3_vm_stop_pc(vm);
    state->last_count = k3_vm_instruction_count(vm);
}

static void test_run_and_callbacks(void)
{
    bt_word program[3] = {
        encode_none(OP_NOP),
        encode_none(OP_NOP),
        encode_none(OP_HALT)
    };
    callback_state state = { 0 };
    k3_vm_callbacks callbacks = {
        &state, record_step, record_stop, NULL
    };
    k3_vm vm;
    uint64_t executed = 99;

    k3_vm_init(&vm, &callbacks);
    CHECK(k3_vm_load(&vm, program, 3, 0));

    CHECK_EQ_I64(k3_vm_run(&vm, 0, &executed), K3_VM_RUNNING);
    CHECK_EQ_I64(executed, 0);
    CHECK_EQ_I64(state.steps, 0);
    CHECK_EQ_I64(state.stops, 0);
    CHECK_EQ_I64(state.last_event, 0);

    CHECK_EQ_I64(k3_vm_run(&vm, 1, &executed), K3_VM_RUNNING);
    CHECK_EQ_I64(executed, 1);
    CHECK_EQ_I64(k3_vm_pc(&vm), 1);
    CHECK_EQ_I64(state.steps, 1);
    CHECK_EQ_I64(state.stops, 0);
    CHECK_EQ_I64(state.last_event, 1);

    CHECK_EQ_I64(k3_vm_run(&vm, 1, &executed), K3_VM_RUNNING);
    CHECK_EQ_I64(executed, 1);
    CHECK_EQ_I64(k3_vm_pc(&vm), 2);
    CHECK_EQ_I64(state.steps, 2);
    CHECK_EQ_I64(state.stops, 0);
    CHECK_EQ_I64(state.last_event, 1);

    CHECK_EQ_I64(k3_vm_run(&vm, 10, &executed), K3_VM_HALTED);
    CHECK_EQ_I64(executed, 1);
    CHECK_EQ_I64(k3_vm_pc(&vm), 3);
    CHECK_EQ_I64(state.steps, 3);
    CHECK_EQ_I64(state.stops, 1);
    CHECK_EQ_I64(state.last_event, 2);
    CHECK_EQ_I64(state.last_status, K3_VM_HALTED);
    CHECK_EQ_I64(state.last_reason, K3_TRAP_NONE);
    CHECK_EQ_I64(state.last_stop_pc, 2);
    CHECK_EQ_I64(state.last_count, 3);

    CHECK_EQ_I64(k3_vm_run(&vm, 10, &executed), K3_VM_HALTED);
    CHECK_EQ_I64(executed, 0);
    CHECK_EQ_I64(state.steps, 3);
    CHECK_EQ_I64(state.stops, 1);
    CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_HALTED);
    CHECK_EQ_I64(state.steps, 3);
    CHECK_EQ_I64(state.stops, 1);

    k3_vm_reset(&vm);
    CHECK_EQ_I64(k3_vm_get_status(&vm), K3_VM_RUNNING);
    CHECK_EQ_I64(k3_vm_pc(&vm), 0);
    CHECK_EQ_I64(k3_vm_instruction_count(&vm), 0);
    CHECK(vm.callbacks.on_step == record_step);
    CHECK(vm.callbacks.on_stop == record_stop);
    check_mem(&vm, 2, word_i64(&program[2]));

    CHECK_EQ_I64(k3_vm_run(&vm, 10, &executed), K3_VM_HALTED);
    CHECK_EQ_I64(executed, 3);
    CHECK_EQ_I64(state.steps, 6);
    CHECK_EQ_I64(state.stops, 2);
    CHECK_EQ_I64(state.last_event, 2);
    CHECK_EQ_I64(state.last_count, 3);

    state = (callback_state){ 0 };
    k3_vm_init(&vm, &callbacks);
    bt_word nop = encode_none(OP_NOP);
    CHECK(k3_vm_set_mem(&vm, K3_VM_MAX_ADDRESS, &nop));
    CHECK(k3_vm_set_pc(&vm, K3_VM_MAX_ADDRESS));

    CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_RUNNING);
    CHECK_EQ_I64(state.steps, 1);
    CHECK_EQ_I64(state.stops, 0);
    CHECK_EQ_I64(state.last_event, 1);
    CHECK_EQ_I64(state.last_status, K3_VM_RUNNING);
    CHECK_EQ_I64(state.last_count, 1);

    CHECK_EQ_I64(k3_vm_step(&vm), K3_VM_TRAPPED);
    CHECK_EQ_I64(state.steps, 1);
    CHECK_EQ_I64(state.stops, 1);
    CHECK_EQ_I64(state.last_event, 2);
    CHECK_EQ_I64(state.last_status, K3_VM_TRAPPED);
    CHECK_EQ_I64(state.last_reason, K3_TRAP_MEMORY);
    CHECK_EQ_I64(state.last_stop_pc, K3_VM_MEMORY_WORDS);
    CHECK_EQ_I64(state.last_count, 1);

    k3_vm_set_callbacks(&vm, NULL);
    CHECK(vm.callbacks.user == NULL);
    CHECK(vm.callbacks.on_step == NULL);
    CHECK(vm.callbacks.on_stop == NULL);
    k3_vm_reset(&vm);
    CHECK_EQ_I64(k3_vm_run(&vm, 1, &executed), K3_VM_RUNNING);
    CHECK_EQ_I64(state.steps, 1);
    CHECK_EQ_I64(state.stops, 1);

    {
        bt_word loop[2] = {
            encode_i4(OP_LDI, 0, 0),
            encode_a(OP_JMP, 0)
        };

        k3_vm_init(&vm, NULL);
        CHECK(k3_vm_load(&vm, loop, 2, 0));
        CHECK_EQ_I64(k3_vm_run(&vm, 5, &executed), K3_VM_RUNNING);
        CHECK_EQ_I64(executed, 5);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 5);
        CHECK_EQ_I64(k3_vm_pc(&vm), 1);
        CHECK_EQ_I64(k3_vm_run(&vm, 0, &executed), K3_VM_RUNNING);
        CHECK_EQ_I64(executed, 0);
        CHECK_EQ_I64(k3_vm_instruction_count(&vm), 5);
    }
}

static void test_kernel_trap_vector_and_eret(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);

    /*
     * Memory layout:
     * 0: LI r0, #50
     * 2: SETVEC r0
     * 3: LDI r1, #10
     * 4: TRAP #7
     * 5: HALT
     *
     * 50 (Kernel Trap Handler):
     * 50: ADDI r1, #20
     * 51: ERET
     */
    bt_word prog[] = {
        encode_i4(OP_LI, 0, 0),
        make_word(50),
        encode_a(OP_SETVEC, 0),
        encode_i4(OP_LDI, 1, 10),
        encode_none(OP_TRAP),
        encode_none(OP_HALT)
    };
    prog[4].t[5] = 1;

    bt_word kernel[] = {
        encode_i4(OP_ADDI, 1, 20),
        encode_none(OP_ERET)
    };

    CHECK(k3_vm_load(&vm, prog, 6, 0));
    CHECK(k3_vm_load(&vm, kernel, 2, 50));

    uint64_t executed = 0;
    CHECK_EQ_I64(k3_vm_run(&vm, 100, &executed), K3_VM_HALTED);
    check_halt(&vm, 5);
    check_reg(&vm, 1, 30);
    CHECK_EQ_I64(vm.trap_vector, 50);
}

int main(void)
{
    test_encoding_examples();
    test_init_and_accessors();
    test_mov_ldi_neg_li();
    test_alu();
    test_shifts();
    test_memory();
    test_control_flow();
    test_overflow_traps();
    test_shift_and_illegal_traps();
    test_fetch_faults();
    test_run_and_callbacks();
    test_kernel_trap_vector_and_eret();

    printf("test_vm: %lu checks, %lu failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
