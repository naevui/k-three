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
    OP_LI = 23,
    OP_ERET = 25,
    OP_SETVEC = 26
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

/* Encode: SETVEC ra */
static void encode_setvec(k3_vm *vm, uint16_t addr, uint8_t ra)
{
    bt_word op;
    bt_word_zero(&op);
    set_base3(op.t, 3, OP_SETVEC);
    set_base3(&op.t[3], 2, ra);
    k3_vm_set_mem(vm, addr, &op);
}

/* Encode: ERET */
static void encode_eret(k3_vm *vm, uint16_t addr)
{
    bt_word op = encode_none(OP_ERET);
    k3_vm_set_mem(vm, addr, &op);
}

static void test_hardware_interrupts(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    /* Trap/Interrupt handler at address 30 */
    /* Code: LI r2, #888; ERET */
    encode_li(&vm, 30, 2, 888);
    encode_eret(&vm, 32);

    /* Main code at address 0 */
    /* Code:
     * 0: LI r0, #30
     * 2: SETVEC r0
     * 3: NOP
     * 4: HALT
     */
    encode_li(&vm, 0, 0, 30);
    encode_setvec(&vm, 2, 0);
    bt_word nop_op = encode_none(OP_NOP);
    k3_vm_set_mem(&vm, 3, &nop_op);
    bt_word halt_op = encode_none(OP_HALT);
    k3_vm_set_mem(&vm, 4, &halt_op);

    /* Test 1: Step to setvec */
    k3_vm_step(&vm); /* executes LI r0, #30 (pc -> 2) */
    k3_vm_step(&vm); /* executes SETVEC r0 (pc -> 3, trap_vector -> 30) */
    CHECK_EQ_I64(k3_vm_get_trap_vector(&vm), 30);

    /* Raise IRQ line 1 manually */
    k3_vm_raise_irq(&vm, 1);
    CHECK_EQ_I64(k3_vm_pending_irqs(&vm), 2); /* bit 1 */

    /* Next step vectors to 30 and executes LI r2, #888 at 30: pc -> 32, r2 -> 888 */
    k3_vm_step(&vm);
    CHECK_EQ_I64(vm.pc, 32);
    CHECK_EQ_I64(helper_get_reg(&vm, 2), 888);
    CHECK_EQ_I64(vm.saved_pc, 3); /* saved return address is NOP at 3 */

    /* Next step executes ERET at 32: restores pc = 3, active_irq = -1 */
    k3_vm_step(&vm);
    CHECK_EQ_I64(vm.pc, 3);
    CHECK_EQ_I64(k3_vm_active_irq(&vm), -1);
    CHECK(k3_vm_irq_enabled(&vm));

    /* Next step executes NOP at 3 */
    k3_vm_step(&vm); /* NOP */
    CHECK_EQ_I64(vm.pc, 4);

    /* Next step executes HALT at 4 */
    k3_vm_step(&vm); /* HALT */
    CHECK_EQ_I64(vm.status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 2), 888);

    /* Test 2: Trigger IRQ via MMIO register write to IO_SYS_IRQ_CTRL */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    encode_li(&vm, 30, 2, 777);
    encode_eret(&vm, 32);

    /* Main code:
     * 0: LI r0, #30
     * 2: SETVEC r0
     * 3: LI r0, #(1 | (1 << 9))   ; enable IRQ and raise line 0 via MMIO
     * 5: LI r1, #5016             ; IO_SYS_IRQ_CTRL
     * 7: STORE r0, [r1]           ; triggers IRQ 0
     * 8: NOP
     * 9: HALT
     */
    encode_li(&vm, 0, 0, 30);
    encode_setvec(&vm, 2, 0);
    encode_li(&vm, 3, 0, (1 | (1 << 9)));
    encode_li(&vm, 5, 1, K3_MMIO_SYS_IRQ_CTRL);
    encode_store(&vm, 7, 0, 1);
    k3_vm_set_mem(&vm, 8, &nop_op);
    k3_vm_set_mem(&vm, 9, &halt_op);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 2), 777);
}

static void test_disk_controller(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    bt_word halt_op = encode_none(OP_HALT);

    /* 1. Query disk capacity: LI r1, #5025; LOAD r0, [r1]; HALT */
    encode_li(&vm, 0, 1, K3_MMIO_DISK_CAPACITY);
    encode_load(&vm, 2, 0, 1);
    k3_vm_set_mem(&vm, 3, &halt_op);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), K3_DISK_MAX_SECTORS);

    /* 2. Write pattern into RAM at 200..203 */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);
    for (int i = 0; i < 4; ++i) {
        bt_word w;
        bt_word_from_i64(100 + i * 11, &w);
        CHECK(k3_vm_set_mem(&vm, (uint16_t)(200 + i), &w));
    }

    /* Program to trigger DMA Write of 4 words from RAM 200 to sector 12:
     * 0: LI r0, #12;   LI r1, #5020; STORE r0, [r1]  ; sector = 12
     * 6: LI r0, #200;  LI r1, #5021; STORE r0, [r1]  ; dma_addr = 200
     * 12: LI r0, #4;   LI r1, #5022; STORE r0, [r1]  ; count = 4
     * 18: LI r0, #2;   LI r1, #5023; STORE r0, [r1]  ; cmd = DISK_CMD_WRITE (2)
     * 24: LI r1, #5024; LOAD r0, [r1]                ; load status
     * 27: HALT
     */
    encode_li(&vm, 0, 0, 12);
    encode_li(&vm, 2, 1, K3_MMIO_DISK_SECTOR);
    encode_store(&vm, 4, 0, 1);

    encode_li(&vm, 5, 0, 200);
    encode_li(&vm, 7, 1, K3_MMIO_DISK_DMA_ADDR);
    encode_store(&vm, 9, 0, 1);

    encode_li(&vm, 10, 0, 4);
    encode_li(&vm, 12, 1, K3_MMIO_DISK_COUNT);
    encode_store(&vm, 14, 0, 1);

    encode_li(&vm, 15, 0, K3_DISK_CMD_WRITE);
    encode_li(&vm, 17, 1, K3_MMIO_DISK_CMD);
    encode_store(&vm, 19, 0, 1);

    encode_li(&vm, 20, 1, K3_MMIO_DISK_STATUS);
    encode_load(&vm, 22, 0, 1);
    k3_vm_set_mem(&vm, 23, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), K3_DISK_STATUS_IDLE);

    /* 3. Clear RAM at 300..303 and DMA Read from sector 12 into RAM 300 */
    k3_vm_reset(&vm);
    k3_io_attach(&vm);

    /* Program to trigger DMA Read from sector 12 into RAM 300:
     * 0: LI r0, #12;   LI r1, #5020; STORE r0, [r1]  ; sector = 12
     * 5: LI r0, #300;  LI r1, #5021; STORE r0, [r1]  ; dma_addr = 300
     * 10: LI r0, #4;   LI r1, #5022; STORE r0, [r1]  ; count = 4
     * 15: LI r0, #1;   LI r1, #5023; STORE r0, [r1]  ; cmd = DISK_CMD_READ (1)
     * 20: LI r1, #5024; LOAD r0, [r1]                ; load status
     * 23: HALT
     */
    encode_li(&vm, 0, 0, 12);
    encode_li(&vm, 2, 1, K3_MMIO_DISK_SECTOR);
    encode_store(&vm, 4, 0, 1);

    encode_li(&vm, 5, 0, 300);
    encode_li(&vm, 7, 1, K3_MMIO_DISK_DMA_ADDR);
    encode_store(&vm, 9, 0, 1);

    encode_li(&vm, 10, 0, 4);
    encode_li(&vm, 12, 1, K3_MMIO_DISK_COUNT);
    encode_store(&vm, 14, 0, 1);

    encode_li(&vm, 15, 0, K3_DISK_CMD_READ);
    encode_li(&vm, 17, 1, K3_MMIO_DISK_CMD);
    encode_store(&vm, 19, 0, 1);

    encode_li(&vm, 20, 1, K3_MMIO_DISK_STATUS);
    encode_load(&vm, 22, 0, 1);
    k3_vm_set_mem(&vm, 23, &halt_op);

    status = k3_vm_run(&vm, 100, &executed);
    CHECK_EQ_I64(status, K3_VM_HALTED);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), K3_DISK_STATUS_IDLE);

    /* Verify RAM at 300..303 matches the original pattern */
    for (int i = 0; i < 4; ++i) {
        bt_word read_w;
        int64_t val = 0;
        CHECK(k3_vm_get_mem(&vm, (uint16_t)(300 + i), &read_w));
        CHECK(bt_word_to_i64(&read_w, &val));
        CHECK_EQ_I64(val, 100 + i * 11);
    }
}

static void test_memory_protection(void)
{
    k3_vm vm;
    k3_vm_init(&vm, NULL);
    k3_io_init();
    k3_io_attach(&vm);

    /* Set user space bounds: 2000..4000 */
    k3_vm_set_user_bounds(&vm, 2000, 4000);
    uint16_t b = 0, l = 0;
    k3_vm_get_user_bounds(&vm, &b, &l);
    CHECK_EQ_I64(b, 2000);
    CHECK_EQ_I64(l, 4000);

    /* 1. In User Mode, code at 2000 loads from 2500 (inside bounds): succeeds */
    bt_word w;
    bt_word_from_i64(555, &w);
    CHECK(k3_vm_set_mem(&vm, 2500, &w));

    /* Code at 2000: LI r1, #2500; LOAD r0, [r1]; */
    encode_li(&vm, 2000, 1, 2500);
    encode_load(&vm, 2002, 0, 1);
    /* In user mode, HALT traps as ILLEGAL, so we leave it to stop or check */
    vm.pc = 2000;
    k3_vm_set_mode(&vm, K3_MODE_USER);

    k3_vm_step(&vm); /* executes LI r1, #2500, pc -> 2002 */
    CHECK_EQ_I64(vm.status, K3_VM_RUNNING);
    k3_vm_step(&vm); /* executes LOAD r0, [r1], pc -> 2003 */
    CHECK_EQ_I64(vm.status, K3_VM_RUNNING);
    CHECK_EQ_I64(helper_get_reg(&vm, 0), 555);

    /* 2. In User Mode, attempting to LOAD from address 500 (below user_base 2000) traps with MEMORY */
    k3_vm_reset(&vm);
    k3_vm_set_user_bounds(&vm, 2000, 4000);
    encode_li(&vm, 2000, 1, 500);
    encode_load(&vm, 2002, 0, 1);
    vm.pc = 2000;
    k3_vm_set_mode(&vm, K3_MODE_USER);

    k3_vm_step(&vm); /* LI r1, #500 */
    k3_vm_step(&vm); /* LOAD r0, [r1] where r1=500 -> TRAP MEMORY! */
    CHECK_EQ_I64(vm.status, K3_VM_TRAPPED);
    CHECK_EQ_I64(vm.trap_reason, K3_TRAP_MEMORY);

    /* 3. In User Mode, attempting to STORE to address 5012 (above user_limit 4000) traps with MEMORY */
    k3_vm_reset(&vm);
    k3_vm_set_user_bounds(&vm, 2000, 4000);
    encode_li(&vm, 2000, 0, 42);
    encode_li(&vm, 2002, 1, K3_MMIO_SYS_LEDS);
    encode_store(&vm, 2004, 0, 1);
    vm.pc = 2000;
    k3_vm_set_mode(&vm, K3_MODE_USER);

    k3_vm_step(&vm); /* LI r0, #42 */
    k3_vm_step(&vm); /* LI r1, #5012 */
    k3_vm_step(&vm); /* STORE r0, [r1] where r1=5012 -> TRAP MEMORY! */
    CHECK_EQ_I64(vm.status, K3_VM_TRAPPED);
    CHECK_EQ_I64(vm.trap_reason, K3_TRAP_MEMORY);
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
    test_hardware_interrupts();
    test_disk_controller();
    test_memory_protection();
    test_fallback_to_memory();

    printf("test_io: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
