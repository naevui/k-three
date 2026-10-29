# Contributing to K-Three (Kord-Three)

Welcome to the **K-Three** project! We are building a software reference model and operating system ecosystem for a balanced-ternary processor concept.

Whether you are fixing a bug, adding new ternary assembly programs, expanding the syscall layer, or enhancing the toolchain, your contributions are welcome.

---

## 1. Project Philosophy & Standards

1. **Pure C11 Standard:**
   * The core virtual machine, toolchain, and libraries require only a standard C11 compiler (`gcc` or `clang`) and POSIX `make`.
   * **Zero external dependencies.** Do not introduce third-party libraries (no ncurses, no external graphics libs). Terminal interaction relies on standard ANSI escape codes and POSIX termios.

2. **Mathematical Purity:**
   * K-Three uses a **balanced-ternary** number system with trits: `-1` (`-`), `0` (`0`), and `+1` (`+`).
   * A data word is exactly 9 trits in the symmetric range `[-9841 .. +9841]`.
   * Memory size is a power of three: $3^8 = 6561$ words (`0 .. 6560`).

3. **Zero-Warning & Zero-Leak Policy:**
   * The project builds with `-std=c11 -Wall -Wextra -Wpedantic`. Code must compile without warnings.
   * All changes must pass cleanly under AddressSanitizer and UndefinedBehaviorSanitizer (`make debug`) with zero memory leaks and zero runtime faults.

---

## 2. Development Workflow

### Building
```sh
# Build static library build/libk3.a and all host tools in build/
make

# Assemble all assembly programs in examples/ and userspace/
make examples

# Clean build artifacts
make clean
```

### Testing
Always verify that your changes pass all unit tests, integration scripts, and sanitizer runs:
```sh
# Run the complete test suite (ternary, VM, ISA, packed, syscalls, examples, debugger)
make test

# Run the complete test suite under AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan)
make debug
```

---

## 3. Repository Structure

* `ternary.h` / `ternary.c`: Public balanced ternary arithmetic API (addition, subtraction, multiplication, shifting).
* `packed.h` / `packed.c`: 2-bit per trit compact binary encoding (18-bit words, 4 words per 9 bytes).
* `isa.h` / `isa.c`: Instruction field layout, opcode definitions (0..26), and instruction decoder.
* `vm.h` / `vm.c`: Processor architectural state (`r0..r7`, PC, 6561-word memory, trap vectors, privilege modes).
* `syscall.h` / `syscall.c`: Standard K-Three host syscall dispatcher (I/O, files, telemetry, desktop).
* `image.h` / `image.c`: Word-image text format reader and writer.
* `disasm.h` / `disasm.c`: Instruction formatting and disassembler core.
* `k3asm.c`: Two-pass assembler (labels, directives, macros, equates).
* `k3vm.c`: Command-line program runner with register/memory dumping and instruction tracing.
* `k3dis.c`: Disassembler CLI.
* `k3dbg.c`: Interactive scriptable debugger with CPU dashboard.
* `k3forth.c`: Minimal Forth compiler and interactive REPL.
* `arch/k3/`: Ternary Linux architecture layer (dmesg boot, trap vectors, task switching).
* `examples/`: K-Three assembly programs (`k3os.s`, `snake.s`, `factorial.s`, `counter.s`, etc.).
* `tests/`: C unit test suites and shell test runners.

---

## 4. Coding Conventions

* **Indentation:** 4 spaces. No tabs in C source files.
* **Naming:**
  * Functions: `k3_<module>_<action>` (e.g. `k3_vm_step`, `k3_host_syscall`).
  * Types: `k3_<name>` or `<name>_t`.
  * Constants / Macros: `K3_<NAME>` in uppercase.
* **Safety:**
  * Always validate buffer capacities and bounds before memory operations.
  * Check memory addresses against `K3_VM_MAX_ADDRESS` (6560).
* **Git Commits:**
  * Use clear, imperative commit messages (e.g., `vm: add privilege mode and trap vectoring`).

---

## 5. Pull Request Guidelines

1. Create a feature branch from `main`.
2. Ensure `make test` and `make debug` pass with **0 failures**.
3. If adding a new syscall or instruction, update `tests/test_syscall.c` or `tests/test_vm.c`.
4. Submit a Pull Request describing your changes and testing results.
