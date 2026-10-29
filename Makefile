CC ?= cc
CFLAGS ?= -O2
CPPFLAGS ?=
INCLUDES := -I.
WARNFLAGS := -Wall -Wextra -Wpedantic
STD := -std=c11

BUILD := build

LIB := $(BUILD)/libk3.a

TOOLS := $(BUILD)/k3asm $(BUILD)/k3vm $(BUILD)/k3dis $(BUILD)/k3dbg $(BUILD)/k3forth $(BUILD)/k3linux
DEBUG_TOOLS := $(BUILD)/k3asm_debug $(BUILD)/k3vm_debug \
               $(BUILD)/k3dis_debug $(BUILD)/k3dbg_debug $(BUILD)/k3forth_debug

TEST_TERNARY := $(BUILD)/test_ternary
TEST_VM := $(BUILD)/test_vm
TEST_ISA := $(BUILD)/test_isa
TEST_PACKED := $(BUILD)/test_packed
TEST_SYSCALL := $(BUILD)/test_syscall
TEST_IMAGE := $(BUILD)/test_image
TEST_TERNARY_DEBUG := $(BUILD)/test_ternary_debug
TEST_VM_DEBUG := $(BUILD)/test_vm_debug
TEST_ISA_DEBUG := $(BUILD)/test_isa_debug
TEST_PACKED_DEBUG := $(BUILD)/test_packed_debug
TEST_SYSCALL_DEBUG := $(BUILD)/test_syscall_debug
TEST_IMAGE_DEBUG := $(BUILD)/test_image_debug

LIB_OBJS := $(BUILD)/ternary.o $(BUILD)/vm.o $(BUILD)/isa.o $(BUILD)/packed.o $(BUILD)/syscall.o $(BUILD)/image.o

TOOL_OBJS := $(BUILD)/k3asm.o $(BUILD)/k3vm.o $(BUILD)/k3dis.o \
             $(BUILD)/k3dbg.o $(BUILD)/image.o $(BUILD)/disasm.o

TEST_TERNARY_OBJS := $(BUILD)/test_ternary.o
TEST_VM_OBJS := $(BUILD)/test_vm.o
TEST_ISA_OBJS := $(BUILD)/test_isa.o
TEST_PACKED_OBJS := $(BUILD)/test_packed.o
TEST_SYSCALL_OBJS := $(BUILD)/test_syscall.o
TEST_IMAGE_OBJS := $(BUILD)/test_image.o

DEBUG_TERNARY_OBJS := $(BUILD)/ternary_debug.o $(BUILD)/test_ternary_debug.o
DEBUG_VM_OBJS := $(BUILD)/ternary_debug.o $(BUILD)/vm_debug.o \
                 $(BUILD)/isa_debug.o $(BUILD)/packed_debug.o \
                 $(BUILD)/syscall_debug.o $(BUILD)/image_debug.o $(BUILD)/test_vm_debug.o
DEBUG_ISA_OBJS := $(BUILD)/ternary_debug.o $(BUILD)/isa_debug.o \
                  $(BUILD)/test_isa_debug.o
DEBUG_PACKED_OBJS := $(BUILD)/ternary_debug.o $(BUILD)/packed_debug.o \
                    $(BUILD)/test_packed_debug.o
DEBUG_SYSCALL_OBJS := $(BUILD)/ternary_debug.o $(BUILD)/vm_debug.o \
                      $(BUILD)/isa_debug.o $(BUILD)/packed_debug.o \
                      $(BUILD)/syscall_debug.o $(BUILD)/image_debug.o $(BUILD)/test_syscall_debug.o
DEBUG_IMAGE_OBJS := $(BUILD)/ternary_debug.o $(BUILD)/packed_debug.o \
                    $(BUILD)/image_debug.o $(BUILD)/test_image_debug.o

DEBUG_FLAGS := -O0 -g3 -fsanitize=address,undefined \
               -fno-omit-frame-pointer

ASAN_ENV := ASAN_OPTIONS=detect_leaks=1 \
            UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1

.PHONY: all tools examples test debug clean

all: $(LIB) $(TOOLS)

tools: $(TOOLS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/ternary.o: ternary.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/vm.o: vm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/isa.o: isa.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/packed.o: packed.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/syscall.o: syscall.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/ternary_debug.o: ternary.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/vm_debug.o: vm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/isa_debug.o: isa.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/packed_debug.o: packed.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/syscall_debug.o: syscall.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/image_debug.o: image.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_ternary.o: tests/test_ternary.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_vm.o: tests/test_vm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_isa.o: tests/test_isa.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_packed.o: tests/test_packed.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_syscall.o: tests/test_syscall.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_image.o: tests/test_image.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_ternary_debug.o: tests/test_ternary.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_vm_debug.o: tests/test_vm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_isa_debug.o: tests/test_isa.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_packed_debug.o: tests/test_packed.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_syscall_debug.o: tests/test_syscall.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/test_image_debug.o: tests/test_image.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/k3asm.o: k3asm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/k3vm.o: k3vm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/k3dis.o: k3dis.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/k3dbg.o: k3dbg.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/k3forth.o: k3forth.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/image.o: image.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/disasm.o: disasm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BUILD)/k3asm: $(BUILD)/k3asm.o $(BUILD)/image.o $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/k3vm: $(BUILD)/k3vm.o $(BUILD)/image.o $(BUILD)/disasm.o $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/k3dis: $(BUILD)/k3dis.o $(BUILD)/image.o $(BUILD)/disasm.o $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/k3dbg: $(BUILD)/k3dbg.o $(BUILD)/image.o $(BUILD)/disasm.o $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/k3forth: $(BUILD)/k3forth.o $(BUILD)/image.o $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/boot.o: arch/k3/boot.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/setup.o: arch/k3/kernel/setup.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/process.o: arch/k3/kernel/process.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

$(BUILD)/k3linux: $(BUILD)/boot.o $(BUILD)/setup.o $(BUILD)/process.o $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_TERNARY): $(TEST_TERNARY_OBJS) $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_VM): $(TEST_VM_OBJS) $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_ISA): $(TEST_ISA_OBJS) $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_PACKED): $(TEST_PACKED_OBJS) $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_SYSCALL): $(TEST_SYSCALL_OBJS) $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_IMAGE): $(TEST_IMAGE_OBJS) $(LIB)
	$(CC) $(STD) $(WARNFLAGS) $(CFLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_TERNARY_DEBUG): $(DEBUG_TERNARY_OBJS)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_VM_DEBUG): $(DEBUG_VM_OBJS)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_ISA_DEBUG): $(DEBUG_ISA_OBJS)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_PACKED_DEBUG): $(DEBUG_PACKED_OBJS)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_SYSCALL_DEBUG): $(DEBUG_SYSCALL_OBJS)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $^ -o $@

$(TEST_IMAGE_DEBUG): $(DEBUG_IMAGE_OBJS)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $^ -o $@

$(BUILD)/k3asm_debug: k3asm.c image.c ternary.c packed.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/k3vm_debug: k3vm.c vm.c isa.c ternary.c packed.c syscall.c image.c disasm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/k3dis_debug: k3dis.c isa.c ternary.c packed.c image.c disasm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/k3dbg_debug: k3dbg.c vm.c isa.c ternary.c packed.c syscall.c image.c disasm.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) $^ -o $@

$(BUILD)/k3forth_debug: k3forth.c vm.c isa.c ternary.c packed.c syscall.c image.c | $(BUILD)
	$(CC) $(STD) $(WARNFLAGS) $(DEBUG_FLAGS) $(CPPFLAGS) $(INCLUDES) $^ -o $@

examples: $(TOOLS)
	@mkdir -p $(BUILD)/examples
	@for src in examples/*.s; do \
		name=$$(basename $$src .s); \
		echo "AS $$src"; \
		$(BUILD)/k3asm $$src -o $(BUILD)/examples/$$name.k3 -l $(BUILD)/examples/$$name.lst; \
	done
	@mkdir -p $(BUILD)/userspace
	@if [ -f userspace/init.s ]; then \
		echo "AS userspace/init.s"; \
		$(BUILD)/k3asm userspace/init.s -o $(BUILD)/userspace/init.k3; \
	fi
	@if [ -f kernel/vmlinux.s ]; then \
		echo "AS kernel/vmlinux.s"; \
		$(BUILD)/k3asm kernel/vmlinux.s -o $(BUILD)/vmlinux.k3 -l $(BUILD)/vmlinux.lst; \
	fi

test: $(TEST_TERNARY) $(TEST_VM) $(TEST_ISA) $(TEST_PACKED) $(TEST_SYSCALL) $(TEST_IMAGE) examples
	./$(TEST_TERNARY)
	./$(TEST_VM)
	./$(TEST_ISA)
	./$(TEST_PACKED)
	./$(TEST_SYSCALL)
	./$(TEST_IMAGE)
	K3ASM=$(abspath $(BUILD)/k3asm) K3DIS=$(abspath $(BUILD)/k3dis) \
	TMP=$(abspath $(BUILD)/asm-tests) sh tests/test_asm.sh
	K3ASM=$(abspath $(BUILD)/k3asm) K3DIS=$(abspath $(BUILD)/k3dis) \
	TMP=$(abspath $(BUILD)/asm-ext-tests) sh tests/test_asm_extended.sh
	K3ASM=$(abspath $(BUILD)/k3asm) K3VM=$(abspath $(BUILD)/k3vm) \
	OUT=$(abspath $(BUILD)/examples) sh tests/test_examples.sh
	K3ASM=$(abspath $(BUILD)/k3asm) K3DBG=$(abspath $(BUILD)/k3dbg) \
	TMP=$(abspath $(BUILD)/dbg-tests) sh tests/test_dbg.sh

debug: $(TEST_TERNARY_DEBUG) $(TEST_VM_DEBUG) $(TEST_ISA_DEBUG) $(TEST_PACKED_DEBUG) $(TEST_SYSCALL_DEBUG) $(TEST_IMAGE_DEBUG) $(DEBUG_TOOLS)
	$(ASAN_ENV) ./$(TEST_TERNARY_DEBUG)
	$(ASAN_ENV) ./$(TEST_VM_DEBUG)
	$(ASAN_ENV) ./$(TEST_ISA_DEBUG)
	$(ASAN_ENV) ./$(TEST_PACKED_DEBUG)
	$(ASAN_ENV) ./$(TEST_SYSCALL_DEBUG)
	$(ASAN_ENV) ./$(TEST_IMAGE_DEBUG)
	$(ASAN_ENV) K3ASM=$(abspath $(BUILD)/k3asm_debug) \
		K3DIS=$(abspath $(BUILD)/k3dis_debug) \
		TMP=$(abspath $(BUILD)/asm-tests-debug) \
		sh tests/test_asm.sh
	$(ASAN_ENV) K3ASM=$(abspath $(BUILD)/k3asm_debug) \
		K3DIS=$(abspath $(BUILD)/k3dis_debug) \
		TMP=$(abspath $(BUILD)/asm-ext-tests-debug) \
		sh tests/test_asm_extended.sh
	$(ASAN_ENV) K3ASM=$(abspath $(BUILD)/k3asm_debug) \
		K3VM=$(abspath $(BUILD)/k3vm_debug) \
		OUT=$(abspath $(BUILD)/examples-debug) \
		sh tests/test_examples.sh
	$(ASAN_ENV) K3ASM=$(abspath $(BUILD)/k3asm_debug) \
		K3DBG=$(abspath $(BUILD)/k3dbg_debug) \
		TMP=$(abspath $(BUILD)/dbg-tests-debug) \
		sh tests/test_dbg.sh

clean:
	rm -rf $(BUILD)
	rm -f k3asm k3vm k3dis k3dbg

-include $(LIB_OBJS:.o=.d) $(TOOL_OBJS:.o=.d) \
         $(TEST_TERNARY_OBJS:.o=.d) $(TEST_VM_OBJS:.o=.d) \
         $(TEST_ISA_OBJS:.o=.d) \
         $(DEBUG_TERNARY_OBJS:.o=.d) $(DEBUG_VM_OBJS:.o=.d) \
         $(DEBUG_ISA_OBJS:.o=.d)
