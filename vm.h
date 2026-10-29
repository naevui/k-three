/*
 * K-Three VM core state, status, traps and public API.
 *
 * The core has no I/O, no filesystem calls and no memory allocation beyond its
 * own structure.  Architecture v0: 8 general registers, one 9-trit PC, 6561
 * words of memory (9 pages of 729 words, 3^8 addressing).
 */

#ifndef K3_VM_H
#define K3_VM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ternary.h"

#define K3_VM_REGISTER_COUNT 8
#define K3_VM_MEMORY_WORDS 6561
#define K3_VM_MAX_ADDRESS (K3_VM_MEMORY_WORDS - 1)

typedef enum k3_vm_status {
    K3_VM_RUNNING = 0,
    K3_VM_HALTED = 1,
    K3_VM_TRAPPED = 2
} k3_vm_status;

typedef enum k3_vm_mode {
    K3_MODE_KERNEL = 0, /* Supervisor / kernel mode: full hardware access */
    K3_MODE_USER = 1    /* User mode: restricted, privileged insns trap */
} k3_vm_mode;

typedef enum k3_trap_reason {
    K3_TRAP_NONE = 0,
    K3_TRAP_OVERFLOW = 1, /* arithmetic result does not fit in 9 trits */
    K3_TRAP_MEMORY = 2,   /* fetch/load/store/jump/branch target out of range */
    K3_TRAP_ILLEGAL = 3,  /* reserved opcode or used register field 8 */
    K3_TRAP_SHIFT = 4,    /* shift amount outside 0..8 */
    K3_TRAP_EXPLICIT = 5  /* TRAP #code */
} k3_trap_reason;

typedef struct k3_vm k3_vm;

typedef struct k3_vm_callbacks {
    void *user;

    /*
     * Called after every instruction that was actually executed, including
     * the instruction that halts or traps.  Not called for a fetch fault,
     * because no instruction was executed.
     */
    void (*on_step)(void *user, const k3_vm *vm);

    /*
     * Called once when the VM transitions to HALTED or TRAPPED, after
     * on_step (if any).  Not called when step() is invoked on an already
     * stopped VM.
     */
    void (*on_stop)(void *user, const k3_vm *vm);

    /*
     * Optional syscall handler called when TRAP #code executes.
     * Returns true if handled (VM stays running), or false to fault/trap.
     */
    bool (*on_syscall)(void *user, k3_vm *vm, int32_t code);
} k3_vm_callbacks;

/*
 * The full state is public so callers can allocate this on the stack and
 * inspect it in tests/debuggers.  Use the accessors for validated writes;
 * direct writes bypass trit/address validation.  Callbacks are observers: they
 * must not re-enter the VM or modify it through a cast.
 */
struct k3_vm {
    bt_word regs[K3_VM_REGISTER_COUNT];
    bt_word memory[K3_VM_MEMORY_WORDS];

    uint16_t pc;       /* next fetch address */
    uint16_t last_pc;  /* most recently executed instruction address */
    uint16_t stop_pc;  /* HALT/TRAP instruction address, or attempted fetch */

    uint64_t instruction_count;

    k3_vm_status status;
    k3_trap_reason trap_reason;
    int32_t trap_code; /* explicit TRAP code; 0 for other stops */

    /* Privilege & Exception Vectoring */
    k3_vm_mode mode;           /* Current privilege mode (KERNEL or USER) */
    k3_vm_mode saved_mode;     /* Mode saved on exception */
    uint16_t saved_pc;         /* PC saved on exception */
    int16_t trap_vector;       /* Kernel trap vector address (-1 = none / host callback) */

    k3_vm_callbacks callbacks;
};

/*
 * Power-on initialization: zero memory, registers, PC, status and counters,
 * and copy the callback table (NULL means no callbacks).
 */
void k3_vm_init(k3_vm *vm, const k3_vm_callbacks *callbacks);

/*
 * CPU reset: clears registers, PC, status, trap state, stop_pc, last_pc and
 * instruction_count.  Memory and callbacks are preserved.  Load a program
 * again only if you want to replace memory.
 */
void k3_vm_reset(k3_vm *vm);

/* Replace the callback table.  The structure is copied. */
void k3_vm_set_callbacks(k3_vm *vm, const k3_vm_callbacks *callbacks);

/*
 * Load words into memory starting at base.  words must not overlap vm->memory.
 * The operation is atomic with respect to validation: if the range is invalid,
 * any word has an invalid trit, or count > 0 with words == NULL, returns false
 * and leaves memory unchanged.  Does not reset the VM or change the PC.
 */
bool k3_vm_load(k3_vm *vm, const bt_word *words, size_t count, uint16_t base);

/* Read/write architectural state with range and trit validation. */
bool k3_vm_get_reg(const k3_vm *vm, uint8_t index, bt_word *out);
bool k3_vm_set_reg(k3_vm *vm, uint8_t index, const bt_word *value);
bool k3_vm_get_mem(const k3_vm *vm, uint16_t address, bt_word *out);
bool k3_vm_set_mem(k3_vm *vm, uint16_t address, const bt_word *value);

uint16_t k3_vm_pc(const k3_vm *vm);
bool k3_vm_set_pc(k3_vm *vm, uint16_t pc);

k3_vm_status k3_vm_get_status(const k3_vm *vm);
k3_trap_reason k3_vm_trap_reason(const k3_vm *vm);
int32_t k3_vm_trap_code(const k3_vm *vm);
uint16_t k3_vm_stop_pc(const k3_vm *vm);
uint16_t k3_vm_last_pc(const k3_vm *vm);
uint64_t k3_vm_instruction_count(const k3_vm *vm);

/*
 * Execute one instruction.  Returns the status after the call.  If the VM is
 * already stopped, returns that status without executing or calling back.
 */
k3_vm_status k3_vm_step(k3_vm *vm);

/*
 * Execute at most max_steps instructions.  Returns the status after the call.
 * If executed is not NULL it receives the number of instructions actually
 * executed by this call (fetch faults do not count).  A return status of
 * K3_VM_RUNNING means the step budget was exhausted.
 */
k3_vm_status k3_vm_run(k3_vm *vm, uint64_t max_steps, uint64_t *executed);

/* Kernel Trap Vector and Privilege Mode API */
void k3_vm_set_trap_vector(k3_vm *vm, int16_t addr);
int16_t k3_vm_get_trap_vector(const k3_vm *vm);
void k3_vm_set_mode(k3_vm *vm, k3_vm_mode mode);
k3_vm_mode k3_vm_get_mode(const k3_vm *vm);
uint16_t k3_vm_get_saved_pc(const k3_vm *vm);

#endif /* K3_VM_H */
