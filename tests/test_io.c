#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io.h"
#include "ternary.h"
#include "vm.h"

enum {
    OP_NOP = 0,
    OP_HALT = 1,
    OP_LOAD = 15,
    OP_STORE = 16,
    OP_LI = 23
};

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(expr) do { \
    g_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

#define CHECK_EQ_I64(a, b) do { \
    g_checks++; \
    int64_t _a = (int64_t)(a); \
    int64_t _b = (int64_t)(b); \
    if (_a != _b) { \
        fprintf(stderr, "%s:%d: check failed: %lld == %lld (%s == %s)\n", \
                __FILE__, __LINE__, (long long)_a, (long long)_b, #a, #b); \
        g_failures++; \
    } \
} while (0)

static int64_t helper_get_reg(const k3_vm *vm, uint8_t r)
{
    bt_word w;
    int64_t val = 0;
    k3_vm_get_reg(vm, r, &w);
    bt_word_to_i64(&w, &val);
    return val;
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

static bt_word encode_r2(unsigned opcode, unsigned a, unsigned b)
{
    bt_word w;
    bt_word_zero(&w);
    set_base3(w.t, 3, opcode);
    set_base3(&w.t[3], 2, a);
    set_base3(&w.t[5], 2, b);
    return w;
}

/* Encode: LI rd, #imm (2 words) */
static void encode_li(k3_vm *vm, uint16_t addr, uint8_t rd, int64_t imm)
{
    bt_word op;
    bt_word lit;
    bt_word_zero(&op);
    set_base3(op.t, 3, OP_LI);
    set_base3(&op.t[3], 2, rd);
    bt_word_from_i64(imm, &lit);
    k3_vm_set_mem(vm, addr, &op);
    k3_vm_set_mem(vm, addr + 1, &lit);
}

/* Encode: LOAD rd, [ra] */
static void encode_load(k3_vm *vm, uint16_t addr, uint8_t rd, uint8_t ra)
{
    bt_word op = encode_r2(OP_LOAD, rd, ra);
    k3_vm_set_mem(vm, addr, &op);
}

/* Encode: STORE rd, [ra] */
static void encode_store(k3_vm *vm, uint16_t addr, uint8_t rd, uint8_t ra)
{
    bt_word op = encode_r2(OP_STORE, rd, ra);
    k3_vm_set_mem(vm, addr, &op);
}

static void test_standard_mmio_read(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    bt_word halt_op = encode_none(OP_HALT);

    /* 1. Read MEMSIZE register at 5015 via LOAD */
    /* Code: LI r1, #5015; LOAD r0, [r1]; HALT */
    encode_li(&vm, 0, 1, K3_MMIO_SYS_MEMSIZE);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), K3_VM_MEMORY_WORDS);

    /* 2. Read RNG_WORD register at 5008 */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    encode_li(&vm, 0, 1, K3_MMIO_RNG_WORD);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    int64_t rng1 = helper_get_reg(&vm, 0);
    CHECK(rng1 >= -K3_WORD_MAX && rng1 <= K3_WORD_MAX);

    /* 3. Read RNG_TRIT register at 5009 */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    encode_li(&vm, 0, 1, K3_MMIO_RNG_TRIT);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    int64_t trit = helper_get_reg(&vm, 0);
    CHECK(trit == -1 || trit == 0 || trit == 1);

    /* 4. Read TIMER_L register at 5004 */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    encode_li(&vm, 0, 1, K3_MMIO_TIMER_L);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    int64_t timer_val = helper_get_reg(&vm, 0);
    CHECK(timer_val >= -K3_WORD_MAX && timer_val <= K3_WORD_MAX);
}

static void test_standard_mmio_write(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    bt_word halt_op = encode_none(OP_HALT);

    /* 1. Write to SYS_LEDS register at 5012 */
    /* Code: LI r0, #42; LI r1, #5012; STORE r0, [r1]; HALT */
    encode_li(&vm, 0, 0, 42);
    encode_li(&vm, 2, 1, K3_MMIO_SYS_LEDS);
    encode_store(&vm, 4, 0, 1);
    k3_vm_set_mem(&vm, 5, &halt_op);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(k3_io_get_leds(), 42);

    /* Now read SYS_LEDS back through MMIO LOAD */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    encode_li(&vm, 0, 1, K3_MMIO_SYS_LEDS);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), 42);

    /* 2. Write to SYS_HALT at 5011 halts the CPU */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    /* LI r0, #1; LI r1, #5011; STORE r0, [r1]; (no HALT insn follows) */
    encode_li(&vm, 0, 0, 1);
    encode_li(&vm, 2, 1, K3_MMIO_SYS_HALT);
    encode_store(&vm, 4, 0, 1);
    /* Next instruction would be NOP */
    bt_word nop_op = encode_none(OP_NOP);
    k3_vm_set_mem(&vm, 5, &nop_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
}

/* Custom MMIO device test */
static int64_t s_custom_dev_reg = 0;

static bool custom_dev_read(void *user, k3_vm *vm, uint16_t addr, bt_word *out)
{
    (void)user;
    (void)vm;
    if (addr == 5100) {
        return bt_word_from_i64(s_custom_dev_reg * 2, out);
    }
    return false;
}

static bool custom_dev_write(void *user, k3_vm *vm, uint16_t addr, const bt_word *val)
{
    (void)user;
    (void)vm;
    if (addr == 5100) {
        int64_t v = 0;
        bt_word_to_i64(val, &v);
        s_custom_dev_reg = v;
        return true;
    }
    return false;
}

static void test_custom_device(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    CHECK(k3_io_register_device(5100, 1, custom_dev_read, custom_dev_write, NULL));

    bt_word halt_op = encode_none(OP_HALT);

    /* Write 15 to custom device at 5100 */
    encode_li(&vm, 0, 0, 15);
    encode_li(&vm, 2, 1, 5100);
    encode_store(&vm, 4, 0, 1);
    k3_vm_set_mem(&vm, 5, &halt_op);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(s_custom_dev_reg, 15);

    /* Read back: custom device returns val * 2 = 30 */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    encode_li(&vm, 0, 1, 5100);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), 30);
}

static void test_fallback_to_memory(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    /* Write normal RAM at address 1000 */
    bt_word w;
    bt_word_from_i64(777, &w);
    CHECK(k3_vm_set_mem(&vm, 1000, &w));

    /* Load from address 1000 through VM: should read 777 because not MMIO */
    encode_li(&vm, 0, 1, 1000);
    encode_load(&vm, 2, 0, 1);
    bt_word halt_op = encode_none(OP_HALT);
    k3_vm_set_mem(&vm, 3, &halt_op);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), 777);
}

int main(void)
{
    test_standard_mmio_read();
    test_standard_mmio_write();
    test_custom_device();
    test_fallback_to_memory();

    printf("test_io: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
