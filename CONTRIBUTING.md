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
* `core/`: K-Three processor architectural state, arithmetic, instruction decoder, MMIO, host syscalls, image reader.
* `tools/`: Toolchain CLI programs (`k3asm`, `k3vm`, `k3dis`, `k3dbg`, `k3forth`, `k3cc`).
* `linux/`: Ternary Linux architecture port (`arch/k3/` port, dmesg boot, trap vectors, preemptive scheduler, procfs, kernel patch).
* `userspace/`: Linux userspace init daemons (`kinit.c`, `kinit.s`, `init.s`).
* `include/`: Standalone C standard library and hardware drivers (`k3libc.h`).
* `examples/`: K-Three assembly and C programs (`c_os.c`, `c_hardware.c`, `k3os.s`, `snake.s`, `factorial.s`, etc.).
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
