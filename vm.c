#define _DEFAULT_SOURCE

#include "vm.h"

#include <stdio.h>
#include "isa.h"

#define K3_USES_A 0x1u
#define K3_USES_B 0x2u
#define K3_USES_C 0x4u

static int64_t word_value(const bt_word *w)
{
    int64_t value = 0;

    (void)bt_word_to_i64(w, &value);
    return value;
}

static void set_word_value(bt_word *w, int64_t value)
{
    (void)bt_word_from_i64(value, w);
}

/*
 * Decode an instruction's 4-trit immediate to a normal 9-trit word.
 */
static bt_word word_from_imm4(int32_t value)
{
    bt_word w;

    (void)bt_word_from_i64((int64_t)value, &w);
    return w;
}

static k3_vm_status set_trap(k3_vm *vm, k3_trap_reason reason,
                             int32_t code, uint16_t stop_pc)
{
    if (vm->trap_vector >= 0 && (size_t)vm->trap_vector <= K3_VM_MAX_ADDRESS) {
        vm->saved_pc = (reason == K3_TRAP_EXPLICIT) ? vm->pc : stop_pc;
        vm->saved_mode = vm->mode;
        vm->mode = K3_MODE_KERNEL;
        vm->trap_reason = reason;
        vm->trap_code = code;
        vm->stop_pc = stop_pc;
        vm->pc = (uint16_t)vm->trap_vector;
        return K3_VM_RUNNING;
    }

    vm->status = K3_VM_TRAPPED;
    vm->trap_reason = reason;
    vm->trap_code = code;
    vm->stop_pc = stop_pc;
    return K3_VM_TRAPPED;
}

static k3_vm_status set_halt(k3_vm *vm, uint16_t stop_pc)
{
    vm->status = K3_VM_HALTED;
    vm->trap_reason = K3_TRAP_NONE;
    vm->trap_code = 0;
    vm->stop_pc = stop_pc;
    return K3_VM_HALTED;
}

static void notify_step(const k3_vm *vm)
{
    if (vm->callbacks.on_step != NULL) {
        vm->callbacks.on_step(vm->callbacks.user, vm);
    }
}

static void notify_stop(const k3_vm *vm)
{
    if (vm->callbacks.on_stop != NULL) {
        vm->callbacks.on_stop(vm->callbacks.user, vm);
    }
}

static unsigned required_fields(unsigned opcode)
{
    switch (opcode) {
    case K3_OP_MOV:
    case K3_OP_NEG:
    case K3_OP_LOAD:
    case K3_OP_STORE:
        return K3_USES_A | K3_USES_B;

    case K3_OP_ADD:
    case K3_OP_SUB:
    case K3_OP_MUL:
    case K3_OP_CMP:
    case K3_OP_SHL:
    case K3_OP_SHR:
        return K3_USES_A | K3_USES_B | K3_USES_C;

    case K3_OP_LDI:
    case K3_OP_ADDI:
    case K3_OP_SUBI:
    case K3_OP_SHLI:
    case K3_OP_SHRI:
    case K3_OP_CMPI:
    case K3_OP_LI:
    case K3_OP_JMP:
    case K3_OP_BRZ:
    case K3_OP_BRNZ:
    case K3_OP_BRN:
    case K3_OP_BRP:
    case K3_OP_SETVEC:
        return K3_USES_A;

    case K3_OP_NOP:
    case K3_OP_HALT:
    case K3_OP_TRAP:
    case K3_OP_ERET:
        return 0;

    default:
        return 0;
    }
}

static bool fields_valid(unsigned opcode, unsigned a, unsigned b, unsigned c)
{
    unsigned required = required_fields(opcode);

    if ((required & K3_USES_A) != 0 && a >= K3_VM_REGISTER_COUNT) {
        return false;
    }
    if ((required & K3_USES_B) != 0 && b >= K3_VM_REGISTER_COUNT) {
        return false;
    }
    if ((required & K3_USES_C) != 0 && c >= K3_VM_REGISTER_COUNT) {
        return false;
    }
    return true;
}

static bool branch_taken(unsigned opcode, int64_t value)
{
    switch (opcode) {
    case K3_OP_BRZ:
        return value == 0;
    case K3_OP_BRNZ:
        return value != 0;
    case K3_OP_BRN:
        return value < 0;
    case K3_OP_BRP:
        return value > 0;
    default:
        return false;
    }
}

static k3_vm_status execute_instruction(k3_vm *vm, const bt_word *ins,
                                        uint16_t pc)
{
    if (!bt_valid(ins->t, K3_TRITS)) {
        return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
    }

    k3_isa_insn in;

    k3_isa_decode(ins, &in);

    if (!k3_isa_known(in.opcode)) {
        return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
    }
    if (!fields_valid(in.opcode, in.a, in.b, in.c)) {
        return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
    }

    switch (in.opcode) {
    case K3_OP_NOP:
        return K3_VM_RUNNING;

    case K3_OP_HALT:
        if (vm->mode == K3_MODE_USER) {
            return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
        }
        return set_halt(vm, pc);

    case K3_OP_MOV:
        vm->regs[in.a] = vm->regs[in.b];
        return K3_VM_RUNNING;

    case K3_OP_LDI:
        set_word_value(&vm->regs[in.a], in.imm);
        return K3_VM_RUNNING;

    case K3_OP_ADD: {
        bt_word result;
        if (bt_add(vm->regs[in.b].t, vm->regs[in.c].t, K3_TRITS,
                   result.t, 0) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_ADDI: {
        bt_word imm = word_from_imm4(in.imm);
        bt_word result;
        if (bt_add(vm->regs[in.a].t, imm.t, K3_TRITS,
                   result.t, 0) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_SUB: {
        bt_word result;
        if (bt_sub(vm->regs[in.b].t, vm->regs[in.c].t, K3_TRITS,
                   result.t, 0) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_SUBI: {
        bt_word imm = word_from_imm4(in.imm);
        bt_word result;
        if (bt_sub(vm->regs[in.a].t, imm.t, K3_TRITS,
                   result.t, 0) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_NEG:
        (void)bt_neg(vm->regs[in.b].t, K3_TRITS, vm->regs[in.a].t);
        return K3_VM_RUNNING;

    case K3_OP_MUL: {
        bt_word low;
        bt_word high;
        if (bt_mul(vm->regs[in.b].t, vm->regs[in.c].t, K3_TRITS,
                   low.t, high.t) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = low;
        return K3_VM_RUNNING;
    }

    case K3_OP_CMP:
        set_word_value(&vm->regs[in.a],
                       bt_cmp(vm->regs[in.b].t, vm->regs[in.c].t, K3_TRITS));
        return K3_VM_RUNNING;

    case K3_OP_SHL: {
        int64_t amount = word_value(&vm->regs[in.c]);
        bt_word result;
        if (amount < 0 || amount > K3_TRITS - 1) {
            return set_trap(vm, K3_TRAP_SHIFT, 0, pc);
        }
        if (bt_shl(vm->regs[in.b].t, K3_TRITS, (size_t)amount,
                   result.t) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_SHLI: {
        bt_word result;
        if (in.imm < 0 || in.imm > (int32_t)(K3_TRITS - 1)) {
            return set_trap(vm, K3_TRAP_SHIFT, 0, pc);
        }
        if (bt_shl(vm->regs[in.a].t, K3_TRITS, (size_t)in.imm,
                   result.t) == BT_OVERFLOW) {
            return set_trap(vm, K3_TRAP_OVERFLOW, 0, pc);
        }
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_SHR: {
        int64_t amount = word_value(&vm->regs[in.c]);
        bt_word result;
        if (amount < 0 || amount > K3_TRITS - 1) {
            return set_trap(vm, K3_TRAP_SHIFT, 0, pc);
        }
        bt_shr(vm->regs[in.b].t, K3_TRITS, (size_t)amount, result.t);
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_SHRI: {
        int64_t amount = in.imm;
        bt_word result;
        if (amount < 0 || amount > K3_TRITS - 1) {
            return set_trap(vm, K3_TRAP_SHIFT, 0, pc);
        }
        bt_shr(vm->regs[in.a].t, K3_TRITS, (size_t)amount, result.t);
        vm->regs[in.a] = result;
        return K3_VM_RUNNING;
    }

    case K3_OP_LOAD: {
        int64_t address = word_value(&vm->regs[in.b]);
        if (address < 0 || address > (int64_t)K3_VM_MAX_ADDRESS) {
            return set_trap(vm, K3_TRAP_MEMORY, 0, pc);
        }
        vm->regs[in.a] = vm->memory[(size_t)address];
        return K3_VM_RUNNING;
    }

    case K3_OP_STORE: {
        int64_t address = word_value(&vm->regs[in.b]);
        if (address < 0 || address > (int64_t)K3_VM_MAX_ADDRESS) {
            return set_trap(vm, K3_TRAP_MEMORY, 0, pc);
        }
        vm->memory[(size_t)address] = vm->regs[in.a];
        return K3_VM_RUNNING;
    }

    case K3_OP_JMP: {
        int64_t target = word_value(&vm->regs[in.a]);
        if (target < 0 || target > (int64_t)K3_VM_MAX_ADDRESS) {
            return set_trap(vm, K3_TRAP_MEMORY, 0, pc);
        }
        vm->pc = (uint16_t)target;
        return K3_VM_RUNNING;
    }

    case K3_OP_BRZ:
    case K3_OP_BRNZ:
    case K3_OP_BRN:
    case K3_OP_BRP:
        if (branch_taken(in.opcode, word_value(&vm->regs[in.a]))) {
            int64_t target = (int64_t)pc + 1 + (int64_t)in.imm;
            if (target < 0 || target > (int64_t)K3_VM_MAX_ADDRESS) {
                return set_trap(vm, K3_TRAP_MEMORY, 0, pc);
            }
            vm->pc = (uint16_t)target;
        }
        return K3_VM_RUNNING;

    case K3_OP_TRAP:
        if (vm->callbacks.on_syscall != NULL &&
            vm->callbacks.on_syscall(vm->callbacks.user, vm, in.imm)) {
            return K3_VM_RUNNING;
        }
        return set_trap(vm, K3_TRAP_EXPLICIT, in.imm, pc);

    case K3_OP_LI: {
        bt_word literal;
        if (vm->pc > K3_VM_MAX_ADDRESS) {
            return set_trap(vm, K3_TRAP_MEMORY, 0, pc);
        }
        literal = vm->memory[vm->pc];
        vm->pc = (uint16_t)(vm->pc + 1);
        vm->regs[in.a] = literal;
        return K3_VM_RUNNING;
    }

    case K3_OP_CMPI: {
        bt_word imm = word_from_imm4(in.imm);
        set_word_value(&vm->regs[in.a],
                       bt_cmp(vm->regs[in.a].t, imm.t, K3_TRITS));
        return K3_VM_RUNNING;
    }

    case K3_OP_ERET: {
        if (vm->mode != K3_MODE_KERNEL) {
            return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
        }
        vm->pc = vm->saved_pc;
        vm->mode = vm->saved_mode;
        return K3_VM_RUNNING;
    }

    case K3_OP_SETVEC: {
        if (vm->mode != K3_MODE_KERNEL) {
            return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
        }
        int64_t target = 0;
        (void)bt_word_to_i64(&vm->regs[in.a], &target);
        if (target < 0 || target > (int64_t)K3_VM_MAX_ADDRESS) {
            return set_trap(vm, K3_TRAP_MEMORY, 0, pc);
        }
        vm->trap_vector = (int16_t)target;
        return K3_VM_RUNNING;
    }

    default:
        return set_trap(vm, K3_TRAP_ILLEGAL, 0, pc);
    }
}

void k3_vm_init(k3_vm *vm, const k3_vm_callbacks *callbacks)
{
    for (size_t i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        bt_word_zero(&vm->regs[i]);
    }
    for (size_t i = 0; i < K3_VM_MEMORY_WORDS; ++i) {
        bt_word_zero(&vm->memory[i]);
    }

    vm->pc = 0;
    vm->last_pc = 0;
    vm->stop_pc = 0;
    vm->instruction_count = 0;
    vm->status = K3_VM_RUNNING;
    vm->trap_reason = K3_TRAP_NONE;
    vm->trap_code = 0;
    vm->mode = K3_MODE_KERNEL;
    vm->saved_mode = K3_MODE_KERNEL;
    vm->saved_pc = 0;
    vm->trap_vector = -1;

    k3_vm_set_callbacks(vm, callbacks);
}

void k3_vm_reset(k3_vm *vm)
{
    for (size_t i = 0; i < K3_VM_REGISTER_COUNT; ++i) {
        bt_word_zero(&vm->regs[i]);
    }

    vm->pc = 0;
    vm->last_pc = 0;
    vm->stop_pc = 0;
    vm->instruction_count = 0;
    vm->status = K3_VM_RUNNING;
    vm->trap_reason = K3_TRAP_NONE;
    vm->trap_code = 0;
    vm->mode = K3_MODE_KERNEL;
    vm->saved_mode = K3_MODE_KERNEL;
    vm->saved_pc = 0;
    vm->trap_vector = -1;
}

void k3_vm_set_callbacks(k3_vm *vm, const k3_vm_callbacks *callbacks)
{
    if (callbacks == NULL) {
        vm->callbacks.user = NULL;
        vm->callbacks.on_step = NULL;
        vm->callbacks.on_stop = NULL;
        vm->callbacks.on_syscall = NULL;
    } else {
        vm->callbacks = *callbacks;
    }
}

bool k3_vm_load(k3_vm *vm, const bt_word *words, size_t count, uint16_t base)
{
    if (words == NULL && count != 0) {
        return false;
    }
    if ((size_t)base > K3_VM_MEMORY_WORDS ||
        count > K3_VM_MEMORY_WORDS - (size_t)base) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        if (!bt_valid(words[i].t, K3_TRITS)) {
            return false;
        }
    }

    for (size_t i = 0; i < count; ++i) {
        vm->memory[(size_t)base + i] = words[i];
    }
    return true;
}

bool k3_vm_get_reg(const k3_vm *vm, uint8_t index, bt_word *out)
{
    if (out == NULL || index >= K3_VM_REGISTER_COUNT) {
        return false;
    }
    *out = vm->regs[index];
    return true;
}

bool k3_vm_set_reg(k3_vm *vm, uint8_t index, const bt_word *value)
{
    if (value == NULL || index >= K3_VM_REGISTER_COUNT ||
        !bt_valid(value->t, K3_TRITS)) {
        return false;
    }
    vm->regs[index] = *value;
    return true;
}

bool k3_vm_get_mem(const k3_vm *vm, uint16_t address, bt_word *out)
{
    if (out == NULL || address > K3_VM_MAX_ADDRESS) {
        return false;
    }
    *out = vm->memory[address];
    return true;
}

bool k3_vm_set_mem(k3_vm *vm, uint16_t address, const bt_word *value)
{
    if (value == NULL || address > K3_VM_MAX_ADDRESS ||
        !bt_valid(value->t, K3_TRITS)) {
        return false;
    }
    vm->memory[address] = *value;
    return true;
}

uint16_t k3_vm_pc(const k3_vm *vm)
{
    return vm->pc;
}

bool k3_vm_set_pc(k3_vm *vm, uint16_t pc)
{
    if (pc > K3_VM_MAX_ADDRESS) {
        return false;
    }
    vm->pc = pc;
    return true;
}

k3_vm_status k3_vm_get_status(const k3_vm *vm)
{
    return vm->status;
}

k3_trap_reason k3_vm_trap_reason(const k3_vm *vm)
{
    return vm->trap_reason;
}

int32_t k3_vm_trap_code(const k3_vm *vm)
{
    return vm->trap_code;
}

uint16_t k3_vm_stop_pc(const k3_vm *vm)
{
    return vm->stop_pc;
}

uint16_t k3_vm_last_pc(const k3_vm *vm)
{
    return vm->last_pc;
}

uint64_t k3_vm_instruction_count(const k3_vm *vm)
{
    return vm->instruction_count;
}

k3_vm_status k3_vm_step(k3_vm *vm)
{
    if (vm->status != K3_VM_RUNNING) {
        return vm->status;
    }

    if (vm->pc > K3_VM_MAX_ADDRESS) {
        k3_vm_status status = set_trap(vm, K3_TRAP_MEMORY, 0, vm->pc);
        notify_stop(vm);
        return status;
    }

    uint16_t pc = vm->pc;
    bt_word instruction = vm->memory[pc];

    vm->last_pc = pc;
    vm->pc = (uint16_t)(pc + 1);
    ++vm->instruction_count;

    k3_vm_status status = execute_instruction(vm, &instruction, pc);

    notify_step(vm);
    if (status != K3_VM_RUNNING) {
        notify_stop(vm);
    }
    return status;
}

k3_vm_status k3_vm_run(k3_vm *vm, uint64_t max_steps, uint64_t *executed)
{
    uint64_t before = vm->instruction_count;

    if (executed != NULL) {
        *executed = 0;
    }
    if (vm->status != K3_VM_RUNNING) {
        return vm->status;
    }

    while (vm->instruction_count - before < max_steps &&
           vm->status == K3_VM_RUNNING) {
        (void)k3_vm_step(vm);
    }

    if (executed != NULL) {
        *executed = vm->instruction_count - before;
    }
    return vm->status;
}

void k3_vm_set_trap_vector(k3_vm *vm, int16_t addr)
{
    if (vm != NULL) vm->trap_vector = addr;
}

int16_t k3_vm_get_trap_vector(const k3_vm *vm)
{
    return vm ? vm->trap_vector : -1;
}

void k3_vm_set_mode(k3_vm *vm, k3_vm_mode mode)
{
    if (vm != NULL) vm->mode = mode;
}

k3_vm_mode k3_vm_get_mode(const k3_vm *vm)
{
    return vm ? vm->mode : K3_MODE_KERNEL;
}

uint16_t k3_vm_get_saved_pc(const k3_vm *vm)
{
    return vm ? vm->saved_pc : 0;
}
