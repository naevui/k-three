# CLAUDE.md - K-Three (Kord-Three) Agent & Developer Guide

This document provides architectural context, commands, and rules for AI assistants (Claude, Cursor, Copilot, etc.) and human developers working in this repository.

---

## 1. Project Overview

K-Three is a software reference model and operating system ecosystem for a **balanced-ternary processor**:
* **Radix:** Base-3 balanced ternary with trits: `-1` (`-`), `0` (`0`), and `+1` (`+`).
* **Word Size:** 9 trits per word ($3^9 = 19683$ states, symmetric range `[-9841 .. +9841]`).
* **Memory Architecture:** Word-addressed flat memory of $3^8 = 6561$ words (`0 .. 6560`).
* **Registers:** 8 general-purpose 9-trit registers (`r0..r7`).
  * By convention: `r6` is Link Register (return address), `r7` is Data Stack Pointer, `r4` is Return Stack Pointer (Forth).
* **Privilege Levels:** `K3_MODE_KERNEL` (0) and `K3_MODE_USER` (1).
* **Hardware Trap Vector:** When a trap or fault occurs, CPU saves PC to `saved_pc`, elevates to `KERNEL_MODE`, and vectors to `trap_vector`.

---

## 2. Essential Commands

### Build & Run
```sh
make                # Build static lib build/libk3.a and tools in build/
make examples       # Assemble all programs in examples/ and userspace/
make test           # Run full test suite (C suites, asm tests, examples, dbg)
make debug          # Run full test suite under ASan + UBSan (zero-leak guarantee)
make clean          # Remove build artifacts
```

### Running Tools & Operating System
```sh
# Boot the Ternary Linux Environment (dmesg -> /sbin/init -> sh)
./build/k3linux

# Boot the K3-OS Shell / Desktop Environment directly
./build/k3vm build/examples/k3os.k3

# Play the Real-Time Ternary Snake Game
./build/k3vm build/examples/snake.k3

# Interactive Forth REPL
./build/k3forth

# Interactive Debugger with CPU Dashboard
./build/k3dbg build/examples/factorial.k3

# Assemble an assembly program
./build/k3asm examples/mycode.s -o build/mycode.k3 -l build/mycode.lst
```

---

## 3. Instruction Set Architecture (ISA v0 - 27 Opcodes)

All instructions occupy **1 word (9 trits)**, except `LI` which takes **2 words**.

| Opcode | Mnemonic | Operands | Description |
|---|---|---|---|
| 0 | `NOP` | - | No operation |
| 1 | `HALT` | - | Stop processor (privileged: traps in USER_MODE) |
| 2 | `MOV` | `ra, rb` | Copy register `ra = rb` |
| 3 | `LDI` | `ra, #imm` | Load 4-trit immediate `[-40..+40]` into `ra` |
| 4 | `ADD` | `rd, ra, rb` | `rd = ra + rb` (wraps, overflow trap on carry) |
| 5 | `ADDI`| `ra, #imm` | `ra = ra + imm` (4-trit imm `[-40..+40]`) |
| 6 | `SUB` | `rd, ra, rb` | `rd = ra - rb` |
| 7 | `SUBI`| `ra, #imm` | `ra = ra - imm` |
| 8 | `NEG` | `ra, rb` | `ra = -rb` (exact negation, never overflows) |
| 9 | `MUL` | `rd, ra, rb` | `rd = ra * rb` (overflow trap if product > 9841) |
| 10 | `CMP` | `rd, ra, rb` | `rd = sign(ra - rb)` (-1, 0, or +1) |
| 11 | `SHL` | `rd, ra, rc` | Shift left by `rc` trits |
| 12 | `SHLI`| `ra, #imm` | Shift left by `imm` trits (0..8) |
| 13 | `SHR` | `rd, ra, rc` | Shift right by `rc` trits (nearest division by $3^k$) |
| 14 | `SHRI`| `ra, #imm` | Shift right by `imm` trits (0..8) |
| 15 | `LOAD`| `rd, [ra]` | `rd = memory[ra]` (0..6560) |
| 16 | `STORE`| `rd, [ra]`| `memory[ra] = rd` (0..6560) |
| 17 | `JMP` | `ra` / `label` | Jump to address in register or label (`LI r5; JMP r5`) |
| 18 | `BRZ` | `ra, offset` | Branch if `ra == 0` (4-trit offset `[-40..+40]`) |
| 19 | `BRNZ`| `ra, offset` | Branch if `ra != 0` |
| 20 | `BRN` | `ra, offset` | Branch if `ra < 0` |
| 21 | `BRP` | `ra, offset` | Branch if `ra > 0` |
| 22 | `TRAP`| `#code` | Explicit software trap / Syscall |
| 23 | `LI`  | `ra, #val` | 2-word load: literal word into `ra` `[-9841..+9841]` |
| 24 | `CMPI`| `ra, #imm` | Compare `ra` with 4-trit imm `[-40..+40]` |
| 25 | `ERET`| - | Exception Return (privileged: restores `pc` & `mode`) |
| 26 | `SETVEC`| `ra` | Set kernel trap vector address to `ra` (privileged) |

### Assembler Pseudoinstructions
* `PUSH rx` $\rightarrow$ `ADDI r7, #-1; STORE rx, [r7]`
* `POP rx` $\rightarrow$ `LOAD rx, [r7]; ADDI r7, #1`
* `CALL target` $\rightarrow$ `LI r6, ret_pc; LI r5, target; JMP r5`
* `RET` $\rightarrow$ `JMP r6`
* `CLR rx` $\rightarrow$ `LDI rx, #0`
* `INC rx` $\rightarrow$ `ADDI rx, #1`
* `DEC rx` $\rightarrow$ `ADDI rx, #-1`

---

## 4. Syscall ABI Convention (`syscall.h`)

* **Syscall Trigger:** `TRAP #code`
* **Calling Convention:**
  * Arguments: `r0`, `r1`, `r2`
  * Return value: `r0` (and `r1` for high-word returns)
  * Caller-saved / Scratch: `r0..r2`, `r5`
  * Preserved: `r3..r7` (unless specified)

Key Syscall Codes:
* `0`: `SYS_EXIT` - Halt VM with code in `r0`
* `1`: `SYS_PUTC` - Print char `r0` to stdout
* `2`: `SYS_GETC` - Read char into `r0` (-1 on EOF)
* `3`: `SYS_PUTNUM` - Print signed decimal from `r0`
* `4`: `SYS_PUTS` - Print null-terminated string at address `r0`
* `5`: `SYS_GETNUM` - Read signed decimal integer into `r0`
* `6`: `SYS_PUTTRITS` - Print 9-trit string for `r0` (`+0-0+0--`)
* `7`: `SYS_GETS` - Read line into buffer `r0`, max len `r1`
* `8`: `SYS_WRITE` - Write `r1` characters from buffer `r0`
* `9`: `SYS_RANDOM` - Random word in `[-9841..+9841]` into `r0`
* `10`: `SYS_RANDOM_TRIT` - Random trit `{-1, 0, +1}` into `r0`
* `11`: `SYS_OPEN` - Open file at path `r0`, mode `r1` (0=r, 1=w, 2=a). Returns `fd`
* `12`: `SYS_CLOSE` - Close file descriptor `r0`
* `13`: `SYS_READ` - Read up to `r2` words from `fd r0` into `buf r1`
* `14`: `SYS_WRITE_FD` - Write `r2` words from `buf r1` to `fd r0`
* `15`: `SYS_SEEK` - Seek `fd r0` to offset `r1`, whence `r2`
* `16`: `SYS_REMOVE` - Delete host file at path `r0`
* `17`: `SYS_TIME` - Monotonic uptime in ms (`r0` low, `r1` high)
* `18`: `SYS_SLEEP` - Sleep for `r0` milliseconds
* `19`: `SYS_CLEAR` - Clear terminal screen (`\e[2J\e[H`)
* `20`: `SYS_SETCURSOR` - Move cursor to row `r0`, col `r1` (1-based)
* `21`: `SYS_KEYHIT` - Non-blocking key check on stdin (1: key ready, 0: none)
* `22`: `SYS_MEMSIZE` - Returns total memory words (6561) into `r0`
* `23`: `SYS_INSN_COUNT` - Total instructions executed
* `26`: `SYS_EXEC` - Spawn & execute child `.k3` program at path `r0`
* `27`: `SYS_PUTTRITS_COLOR` - Colored trits (`+` green, `-` red, `0` dim)
* `28`: `SYS_HOST_INFO` - Host OS, hardware, and date string
* `33`: `SYS_DRAW_VRAM` - Render VRAM buffer at `r0` (w=`r1`, h=`r2`)
* `34`: `SYS_GUI_DESKTOP` - Launch interactive 4-tab Desktop Environment
* `35`: `SYS_RAW_MODE` - Enable/disable raw unbuffered terminal mode
* `36`: `SYS_RENAME` - Rename a file (`r0` = old path, `r1` = new path)
* `37`: `SYS_FILE_SIZE` - File size in bytes (`r0` = path, returns size in `r0`)
* `38`: `SYS_FILE_EXISTS` - Check file existence (`r0` = path, returns 1/0)
* `39`: `SYS_BANK_SWITCH` - Bank switching (`r0` = slot 0..7, `r1` = bank ID 0..2047, returns old bank)

---

## 5. Critical Code Guidelines & Gotchas

1. **Immediate Boundaries:**
   * Branch offsets (`BRZ`, `BRNZ`, `BRN`, `BRP`) and ALU immediates (`ADDI`, `SUBI`, `LDI`, `CMPI`) **must fit in 4 trits (`-40 .. +40`)**.
   * For larger jumps or values, use `JMP target_label` (3 words) or `LI rx, #val` (2 words).
2. **String Escape Sequences:**
   * In `.string` directives and assembler strings, use `\e` for escape character (`27`). Avoid `\033` because `\0` will be parsed as a string-terminating null byte.
3. **Memory Limits:**
   * Maximum valid memory address is `K3_VM_MAX_ADDRESS = 6560` ($3^8 - 1$).
   * Any fetch, load, store, or jump outside `0 .. 6560` triggers a `K3_TRAP_MEMORY` fault.
4. **C Standard Compliance:**
   * Strictly `-std=c11 -Wall -Wextra -Wpedantic`. Never use non-standard extensions or implicit function declarations.
   * Do not introduce external library dependencies.
