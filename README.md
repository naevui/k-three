# K-Three (Kord-Three)

An experimental balanced-ternary processor architecture, virtual machine, toolchain, and operating system.

**Status: architecture, VM, toolchain, OS and debugger.** The ternary/word
library, its tests, the ISA v0 proposal, the VM core, a shared instruction
decoder, the assembler/loader/disassembler/debugger toolchain, a 35-call host
syscall layer and a small interactive operating system with example programs
exist. Everything here is a reference model; there are no performance claims.

See [`ISA.md`](ISA.md) for the proposed instruction set, encoding, traps and
assembly syntax. The VM core implements ISA v0; the ISA still remains a
proposal.

## Build and test

Requires a C11 compiler and `make`; no external libraries. POSIX `sh` and
`grep` are used by the script-based test suites.

```sh
make          # build build/libk3.a and the tools in build/
make test     # ternary, VM and decoder suites, then the tool/example scripts
make debug    # the same under ASan + UBSan
make examples # only assemble examples/*.s into build/examples/
make clean
```

The build uses `-std=c11 -Wall -Wextra -Wpedantic`. `make debug` uses
`-O0 -g3 -fsanitize=address,undefined`.

## Tools

All four tools read or write a word image. The default text image holds one or
more signed decimal 9-trit words per line, separated by whitespace or commas.
`;`, `#` at the start of a token, and `//` start comments. This is a debugging
format, not part of the ISA. The shared reader/writer lives in
`image.h`/`image.c`.

Assembler output can also be written as a compact binary `.k3b` image: a
16-byte header (`\x7FK3B`, version, entry point, word count, payload size)
followed by the 2-bit-per-trit packed stream from `packed.c` (4 words per 9
bytes). `k3vm`, `k3dis` and `k3dbg` detect the magic and load either format
transparently.

```sh
# Assemble in.s to in.k3 (default output replaces .s with .k3). -l writes a
# simple address/word/trit listing.
build/k3asm examples/factorial.s -o build/factorial.k3
build/k3asm examples/factorial.s -o build/factorial.k3 -l build/factorial.lst

# Emit a compact binary image instead (or name the output *.k3b).
build/k3asm -b examples/factorial.s -o build/factorial.k3b

# Disassemble; -t also prints the raw trits of each word, -l marks branch
# targets with synthetic Lnnnn labels.
build/k3dis -t build/factorial.k3
build/k3dis -l build/factorial.k3

# Run; -d dumps registers, -m dumps non-zero memory, -t traces each decoded
# instruction to stderr.
build/k3vm -d -m build/factorial.k3
build/k3vm -t build/factorial.k3

# Scriptable debugger.  Prompts and diagnostics go to stderr, so stdout is
# clean for scripts; -q suppresses the banner and prompts.
printf 'break 5\ncontinue\nregs\ndis 5 3\nquit\n' |
    build/k3dbg -q build/factorial.k3
```

`k3asm` supports `label:` definitions, labels as branch targets, forward
references, the `.word N`, `.equ NAME, VALUE`, `.include "file"`, `.string
"text"` and `.space N` directives, plus the `jmp label`, `clr`, `inc`, `dec`,
`push`, `pop` and `ret` pseudoinstructions. It reports errors with
`file:line`. Out-of-range immediates, reserved register `r8`, unknown labels
and unknown mnemonics are rejected and no output file is written.
`-l`/`--list` writes an address/word/trit listing to a file; `-b`/`--binary`
selects the compact `.k3b` output.

`k3dbg` commands are `step [N]`, `continue`, `regs`, `reg rN`, `set rN VALUE`,
`set pc ADDR`, `mem ADDR [N]`, `setmem ADDR VALUE`, `dis [ADDR [N]]`,
`break ADDR`, `delete [ADDR]`, `info`, `reset`, `help` and `quit`. `break`
with no argument lists breakpoints; `delete` with no argument clears all of
them. `-l` enables labels in `dis`, and `-s N` sets the `continue` step limit
(default 1,000,000). `reset` reinitialises the CPU and reloads the image.

### `k3cc` - C-subset compiler

`k3cc` compiles a small C dialect straight to K-Three assembly, which is then
assembled with `k3asm` and executed on `k3vm`:

```sh
build/k3cc examples/hello_c.c -o build/hello_c.s
build/k3asm build/hello_c.s -o build/hello_c.k3
build/k3vm build/hello_c.k3
```

Supported language:

| Feature | Notes |
|---|---|
| Types | `int` only (one 9-trit word, `-9841 .. +9841`) |
| Variables | globals with constant initialisers, locals, block scoping |
| Arrays | `int a[N];` or `int a[N] = {c1, c2, ...};` as globals or locals, `a[i]` read/write, no bounds checks |
| Functions | up to 5 `int` parameters, `return`, recursion |
| Statements | `if` / `else`, `while`, `for`, `break`, `continue`, blocks, expression statements |
| Operators | `+ - * / %`, unary `-`, `=`, `< > <= >= == !=` |
| Builtins | `print(x)`, `putc(c)`, `puts("...")`, `halt()` |
| Comments | `// line` and `/* block */` |

Calling convention: arguments arrive in `r0..r4`, the result is returned in
`r0`, `r6` is the link register and `r3` is the frame pointer. `r7` is the
stack pointer, initialised to 6560 (the last word of memory) and growing
downwards. The compiler saves `r6`/`r3` around every call and pops the whole
frame on `return`, so recursion and nested calls work. Conditional branches
longer than the 4-trit range of `BRZ`/`BRN` are expanded into an inverted
short branch over an absolute `JMP`.

Locals are block scoped: leaving a block pops the words its declarations
pushed, and `break`/`continue` pop back to the locals live at loop entry, so
declaring variables inside loops does not leak stack space.

Arrays are fixed-size: `int a[10];` reserves ten words, either in the global
data section or on the stack frame. `a[i]` computes `base + i` with no bounds
checking. Both globals and locals accept a constant brace initialiser such as
`int a[4] = {10, 20};`; elements after the last supplied value are zero-filled,
and supplying more elements than the array holds is a compile error.

Known limits: no pointers or `char`; a function accepts at most five
parameters. `/` and `%` truncate toward zero and are implemented by a small
`__k3_divmod` helper that is emitted only when used; division by zero returns
`0` instead of trapping.

String literals are supported only as `puts("...")` arguments. They are
emitted as null-terminated `.string` data at the end of the program and
printed with `SYS_PUTS` (trap 4). `\n`, `\t`, `\r`, `\0`, `\\` and `\"`
escapes are passed through to `k3asm`.

Example programs live in `examples/`:

| File | What it demonstrates | Expected result |
|---|---|---|
| `counter.s` | countdown loop | `r0 = 0` |
| `factorial.s` | `MUL` loop, 7! | `r1 = 5040` |
| `loop_label.s` | label branch target, sum 1..6 | `r1 = 21` |
| `memtest.s` | `STORE`/`LOAD` round trip | `mem[10] = 123` |
| `isa_example.s` | the example from `ISA.md` section 8 | `r1 = 728` |
| `overflow.s` | overflow trap | exits 2, reason `overflow` |
| `primes.s` | trial-division prime finder up to 41 | `r1 = 13`, `r2 = 41`, `r3 = 238` |
| `fib.s` | 20 Fibonacci numbers up to F(19) | `r1 = 20`, `r2 = 4181`, `r3 = 2584` |
| `syscall_demo.s` | host syscalls and OS interface | prints telemetry |
| `k3os.s` | interactive K3-OS shell (`calc`, `save`, `cat`, `exec`, GUI) | prompts until `q` |
| `snake.s` | real-time ternary Snake game | `q` quits |
| `hello_c.c` | C source compiled by `k3cc` (recursive factorial) | prints `120`, `r0 = 120` |

`k3vm` exit status: `0` halted, `2` trapped, `3` step limit, `1` usage/load
error.


## Layout

```
ternary.h            public arithmetic API
ternary.c            implementation
packed.h             packed 2-bit-per-trit word/stream API
packed.c             implementation
isa.h                instruction layout, opcode enum and decoder API
isa.c                decoder implementation (core library, no I/O)
vm.h                 VM core state, privilege modes, status/trap API, callbacks
vm.c                 VM execution core (uses isa.h)
image.h              text and .k3b word-image reader/writer (host tools)
image.c              implementation
disasm.h             instruction formatting (host tools, uses stdio)
disasm.c             implementation
syscall.h            host syscall numbers and ABI
syscall.c            syscall layer, terminal control and GUI desktop
k3asm.c              assembler (labels, directives, pseudoinstructions)
k3vm.c               command-line loader/runner
k3dis.c              disassembler
k3dbg.c              scriptable interactive debugger
k3forth.c            Forth compiler for the K-Three VM
k3cc.c               C-subset compiler (emits K-Three assembly)
arch/k3/             Linux-style architecture layer (boot, setup, process, entry)
kernel/vmlinux.s     bootable kernel image source
userspace/init.s     PID 1 init process
tests/test_ternary.c exhaustive, edge-case and random arithmetic tests
tests/test_vm.c      opcode, trap, control-flow and callback tests
tests/test_isa.c     decoder field, opcode and immediate tests
tests/test_packed.c  packed word/stream round-trip tests
tests/test_syscall.c host syscall ABI tests
tests/test_asm.sh    assembler encoding and error-handling tests
tests/test_asm_extended.sh literals, directives, pseudoinstructions and .k3b
tests/test_examples.sh assembles and runs every example program
tests/test_dbg.sh    scripted debugger command tests
tests/test_c.sh      C compiler (k3cc) end-to-end tests
examples/*.s         K-Three assembly examples
ISA.md               ISA v0 proposal and open questions
Makefile
```

## Representation

* One balanced trit per `int8_t`, value `-1`, `0` or `+1`. No packing.
* Trit order is least significant first: `t[0]` has weight `3^0`.
* An `n`-trit value represents exactly the integers in
  `[-(3^n - 1) / 2, +(3^n - 1) / 2]`.
* A K-Three word is `K3_TRITS = 9` trits, range `-9841 .. +9841`.
* The API is generic over `n` so the same code can be exhaustively tested at
  4 trits and used at 9 trits.

The functions assume every input trit is valid (`-1`, `0`, `+1`) unless stated
otherwise. `bt_valid()` checks that precondition.

## Arithmetic semantics

All results are written as canonical balanced-ternary trits, not as host
integers.

| Operation | Result | Overflow indication |
|---|---|---|
| `bt_add` | low `n` trits of `a + b` | carry trit `c`, `BT_OVERFLOW` iff `c != 0` |
| `bt_sub` | low `n` trits of `a - b` | carry trit `c`, `BT_OVERFLOW` iff `c != 0` |
| `bt_neg` | exact `-a` | never overflows (range is symmetric) |
| `bt_mul` | exact product split into low/high `n` trits | `BT_OVERFLOW` iff the high half is nonzero |
| `bt_shl` | low `n` trits of `a * 3^amount` | `BT_OVERFLOW` iff nonzero trits were shifted out |
| `bt_shr` | `a` with `amount` low trits dropped | never overflows |

Exact relations:

```
add:  value(a) + value(b) = value(out) + c * 3^n
sub:  value(a) - value(b) = value(out) + c * 3^n
mul:  value(a) * value(b) = value(out) + value(high) * 3^n
```

Addition/subtraction **wrap** to the low `n` trits and report the carry; they
do not saturate and do not trap. Example at 4 trits:

```
40 + 40 = 80 = (-1) + 1*3^4  ->  out = -1, carry = +1, BT_OVERFLOW
```

`bt_mul` always writes the complete exact product, split into two `n`-trit
halves. `high` is required and must not overlap the inputs or `out`.

`bt_shl` is a pure digit shift, so it corresponds to multiplication by a power
of three. `bt_shr` is **not** truncation toward zero; dropping low balanced
trits is division rounded to nearest (ties are impossible because `3^k` is
odd):

```
 5 >> 1 =  2
-5 >> 1 = -2
 4 >> 1 =  1
```

For `amount >= n`, both shifts produce zero; `bt_shl` reports overflow iff the
input was nonzero.

Conversion behavior:

* `bt_from_i64` returns `false` and zeroes the output if the value does not fit
  in `n` trits.
* `bt_to_i64` returns `false` and writes `0` if the trit array is invalid or
  the value does not fit in `int64_t`.

Aliasing: `bt_add`, `bt_sub` and `bt_neg` may write into their input. `bt_mul`,
`bt_shl` and `bt_shr` require non-overlapping buffers.

## VM core

`vm.h` defines the complete architectural state (`r0..r7`, PC, 6561-word memory,
status, trap reason/code and instruction count) plus a small callback table.
The state is public so a caller can allocate the VM on the stack and inspect it;
the accessors add range/trit validation for writes. The core is deliberately
free of `stdio`, file and OS calls. Host tracing and stop notification go
through `on_step`/`on_stop` callbacks, which receive a `const k3_vm *`.

* `k3_vm_init` performs power-on initialization and zeroes memory.
* `k3_vm_reset` clears the CPU state (registers, PC, status, traps and counters)
  but preserves memory and callbacks.
* `k3_vm_load` validates the whole destination range and every trit before
  copying, so a rejected load leaves memory unchanged.
* `k3_vm_step` executes one instruction; `k3_vm_run(max_steps, &executed)`
  executes up to a budget and reports the status after the call. A return of
  `K3_VM_RUNNING` means the budget was exhausted.
* If an instruction leaves the next fetch address at 6561 (for example, a
  non-branch instruction at address 6560), the next step reports a memory fault
  at the attempted fetch. This matches the ISA's fetch rule; no instruction at
  6561 is executed.
* The core tracks kernel/user privilege, a trap vector (`SETVEC`) and returns
  from traps (`ERET`). Host I/O is still delivered through the callback table,
  not through program-visible registers.

## Test coverage

`make test` currently performs:

Arithmetic suite:

* Exhaustive conversion round-trips for all 81 representable 4-trit values and
  all 19,683 representable 9-trit values.
* Exhaustive 4-trit operand pairs (81 x 81) for add, sub, mul, compare and neg,
  checked against an independent integer reference.
* Exhaustive 4-trit shifts for shift counts 0..5.
* All pairs from a 9-trit edge-value set containing zero, +/-1, powers of three,
  powers-of-three boundaries, and the word limits +/-9841.
* 50,000 deterministic random 9-trit operand pairs (including shifts) checked
  against the same integer reference.
* Host-range edges: `INT64_MIN`, `INT64_MAX`, 40/41-trit boundaries, invalid
  trits, zero-width operations, and huge shift counts.

VM suite:

* Every opcode, both reserved opcodes, and used register field 8.
* Arithmetic/shift overflow and range traps, memory faults, illegal
  instructions and explicit traps, including no-write atomicity checks.
* Branch conditions and offsets, invalid taken targets, valid/invalid absolute
  jumps, two-word `LI`, the fetch edge at address 6560, a missing `LI` literal,
  and a self-modifying write to the next instruction.
* Callback ordering/counts, `run` budgets, reset/init behavior, state accessors
  and atomic loader validation.
* The four encoding examples in `ISA.md`, checked trit-for-trit.

Decoder suite (`tests/test_isa.c`):

* The four `ISA.md` encodings, including the unused field values that a raw
  instruction word produces.
* Every opcode value `0..26`, every register field value `0..8`, and every
  4-trit immediate value `-40..40`.
* Reserved opcodes, mnemonic lookup and 4-trit range rejection.

Assembler, example and debugger suites (`tests/test_asm.sh`,
`tests/test_asm_extended.sh`, `tests/test_examples.sh`, `tests/test_dbg.sh`):

* The four `ISA.md` encodings, re-assembled and checked trit-for-trit.
* Comment forms, `#imm` handling, case-insensitivity, forward labels, label
  aliases in the same line, `LI` size and literal range, `.word`, `.equ`,
  `.include`, `.string`, `.space`, character/ternary literals and
  pseudoinstructions.
* `.k3b` binary images: magic bytes, packed round-trip through `k3dis`/`k3vm`,
  and the default output name selected by `-b`.
* Rejection of reserved `r8`, out-of-range immediates, shift counts, unknown
  labels/mnemonics, wrong operand counts and duplicate labels, with no output
  file written on error.
* Every example is assembled and run; expected register, memory and exit-status
  values are checked, including the decoded `-t` trace.
* Debugger initial state, stepping, breakpoints, continues, halt/trap status,
  register/memory/pc edits, memory dumps, labeled disassembly, `LI` literal
  skipping, the step limit, unknown commands, reset and image-load errors.

C compiler suite (`tests/test_c.sh`):

* Recursive factorial, `while` and `for` loops, global and local variables,
  multi-argument calls, `else if` chains, nested calls inside expressions,
  `print()` output, branch distances beyond the 4-trit conditional range and
  globals declared between functions.

All suites also run under AddressSanitizer and UndefinedBehaviorSanitizer via
`make debug`.

Current result: `test_ternary` has `1,762,135` checks, `test_vm` has `7,398`
checks, `test_isa` has 257 checks, `test_packed` has `118,144` checks,
`test_syscall` has 170 checks and `test_image` has 64 checks, all 0 failures;
`test_asm` has 23 checks, `test_asm_extended` has 7, `test_examples` has 13,
`test_dbg` has 38 and `test_c` has 9, all 0 failures. The exact counts are
printed by the test binaries and scripts.

## 2-bit-per-trit packing (implemented for images)

`packed.h`/`packed.c` implement this layout for `.k3b` images and for
memory-image packing; the VM core still keeps one `int8_t` trit per element.

| Bits | Trit |
|---|---|
| `00` | 0 |
| `01` | +1 |
| `10` | -1 |
| `11` | invalid / reserved |

* Word width is 18 bits; trit 0 occupies the least significant bit pair.
* Negation is a swap of `01` and `10`; `00` stays `00`. `11` has no defined
  negation.
* `bt_pack_stream` writes four words into nine bytes (72 bits, no padding) and
  `bt_unpack_stream` rejects invalid `11` pairs through `bt_word_unpack`.
* Arithmetic still runs on the unpacked one-byte-per-trit representation; the
  packed form is an interchange/storage format, not the datapath.
* The packing round-trip is covered by `tests/test_packed.c` and the `.k3b`
  tests in `tests/test_asm_extended.sh`.

## Decisions proposed for review

These are library-level choices, not yet ISA decisions. They are called out
here so they are not silent:

1. **Generic `n`-trit API** in addition to the 9-trit word type, so 4-trit
   exhaustive tests exercise the same code used for 9-trit words.
2. **Wrapping add/sub with a carry trit** and a status enum, rather than
   saturation or a hard trap.
3. **Full 2n-trit multiply** split into `out`/`high`, with overflow defined as
   a nonzero high half.
4. **Balanced right shift** (drop low trits) rather than truncation toward
   zero.
5. **Failure zeroes outputs** for conversions, so callers never see a
   partially converted value.
6. **Aliasing rules** as listed above.

The ISA resolves this at the architectural level by faulting on overflow (see
`ISA.md` section 5); the library itself still wraps/reports a carry or overflow
status so other callers can choose a different policy.

## VM implementation choices

These are host-API choices, not new ISA semantics:

1. The full VM state is public so callers can allocate it without an allocator.
2. Callback tables are copied by value; `NULL` means no callbacks.
3. `k3_vm_load` is atomic with respect to range and trit validation.
4. `k3_vm_reset` preserves memory and callbacks, while `k3_vm_init` zeroes
   memory.
5. An instruction that does not replace PC at address 6560 may leave PC at 6561
   and status running; the next step traps before executing anything.
