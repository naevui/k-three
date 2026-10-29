# K-Three ISA v0

Status: **proposal, implemented by the reference VM core.** The VM core exists
and is tested; the assembler, CLI runner and example programs are not written
yet. The open questions at the end are deliberate; they are not silent
decisions.

This is a reference-model ISA. It makes no performance claims and says nothing
about a hardware implementation.

## 1. Architectural state

| State | Size | Notes |
|---|---|---|
| General registers | `r0`..`r7`, 8 x 9 trits | All are general; no hardwired zero register |
| Program counter | one 9-trit value | Next fetch address; valid fetch range `0..728` |
| Memory | 729 words x 9 trits | Flat, word-addressed, shared code/data |
| Status | running / halted / trapped | VM-level state; not a program-visible register |

There are no condition-code flags, no stack pointer, no link register, no
privilege levels, and no program-visible I/O registers in v0.

A data word is a balanced-ternary value in `-9841..+9841`. Trit 0 is the least
significant trit throughout this document: word value is
`sum(t[i] * 3^i)` for `i = 0..8`.

Registers and memory words hold data words. Instruction words use their trits
as encoded fields (section 3).

## 2. Memory and addresses

* 729 words, numbered `0..728`.
* An address is the numerical value of a 9-trit word. Only `0..728` are valid.
  A negative address or an address `>= 729` is a memory fault.
* Memory is word-addressed. There is no byte addressing and no alignment rule.
* Program and data share the same memory. Instruction fetch reads the word at
  the PC.
* There is no memory protection. Self-modifying code is possible; see the open
  questions.
* The VM starts with zeroed memory and a loader writes the program image. The
  image format is outside this ISA document.

## 3. Instruction word encoding

Every instruction is one 9-trit word, except `LI`, which is two words.

### 3.1 Field layout

```text
trit index:   8   7   6   5   4   3   2   1   0
field:       C1  C0  B1  B0  A1  A0  OP2 OP1 OP0
```

Fields are numbered from the least significant trit. Multi-trit integer
instruction fields use **base-3 digits with trit = digit - 1**:

* Opcode field `OP` (3 trits, `t[0..2]`):
  `op = (t0+1) + 3*(t1+1) + 9*(t2+1)`, range `0..26`.
* Register field `A` (2 trits, `t[3..4]`):
  `a = (t3+1) + 3*(t4+1)`, range `0..8`.
* Register field `B` (2 trits, `t[5..6]`):
  `b = (t5+1) + 3*(t6+1)`, range `0..8`.
* Register field `C` (2 trits, `t[7..8]`):
  `c = (t7+1) + 3*(t8+1)`, range `0..8`.

Register field value `8` is reserved. If an instruction actually uses a
register field whose value is `8`, the instruction faults as illegal.

### 3.2 Register encoding

| Register | Field trits (low, high) | Base-3 value |
|---|---|---|
| `r0` | `-1 -1` | 0 |
| `r1` | ` 0 -1` | 1 |
| `r2` | `+1 -1` | 2 |
| `r3` | `-1  0` | 3 |
| `r4` | ` 0  0` | 4 |
| `r5` | `+1  0` | 5 |
| `r6` | `-1 +1` | 6 |
| `r7` | ` 0 +1` | 7 |
| reserved | `+1 +1` | 8 |

The two trits are written in field order `(t_low, t_high)`.

### 3.3 Immediate fields

Some instruction forms use the four trits `t[5..8]` as a **signed balanced**
4-trit immediate, not as two base-3 register fields:

```text
imm4 = t5 + 3*t6 + 9*t7 + 27*t8        range -40..+40
```

The same four trits are used as a branch offset in branch instructions.

`LI` uses the following memory word as a full 9-trit literal; its `B` and `C`
fields are unused.

### 3.4 Unused fields

Unused fields are ignored by the decoder. The canonical assembler encoding is
raw trit `0` in every unused trit position. Note that raw `0,0` in a **used**
register field means `r4`, not "no register".

### 3.5 Instruction forms

| Form | Fields | Used by |
|---|---|---|
| `N` | none | `NOP`, `HALT` |
| `R3` | `A=rd`, `B=ra`, `C=rb` | `ADD`, `SUB`, `MUL`, `CMP`, `SHL`, `SHR` |
| `R2` | `A=rd`, `B=ra`, `C` unused | `MOV`, `NEG` |
| `I4` | `A=rd`, `t[5..8]=imm4` | `LDI`, `ADDI`, `SUBI`, `SHLI`, `SHRI`, `CMPI` |
| `M` | `A=rd`, `B=ra`, `C` unused | `LOAD`, `STORE` |
| `J` | `A=ra`, `B`,`C` unused | `JMP` |
| `B` | `A=ra`, `t[5..8]=offset4` | `BRZ`, `BRNZ`, `BRN`, `BRP` |
| `I4/no-A` | `A` unused, `t[5..8]=code4` | `TRAP` |
| `W` | `A=rd`, `B`,`C` unused, next word = literal | `LI` |

For `LOAD`/`STORE`, `ra` holds the memory address. For `JMP` and branches,
`ra` holds the tested value or target address.

## 4. Opcode table

| # | Mnemonic | Form | Assembly | Operation |
|---|---|---|---|---|
| 0 | `NOP` | N | `nop` | No effect |
| 1 | `HALT` | N | `halt` | Stop with status halted |
| 2 | `MOV` | R2 | `mov rd, ra` | `rd = ra` |
| 3 | `LDI` | I4 | `ldi rd, #i4` | `rd =` sign-extended `i4` |
| 4 | `ADD` | R3 | `add rd, ra, rb` | `rd = ra + rb`; overflow faults |
| 5 | `ADDI` | I4 | `addi rd, #i4` | `rd = rd + i4`; overflow faults |
| 6 | `SUB` | R3 | `sub rd, ra, rb` | `rd = ra - rb`; overflow faults |
| 7 | `SUBI` | I4 | `subi rd, #i4` | `rd = rd - i4`; overflow faults |
| 8 | `NEG` | R2 | `neg rd, ra` | `rd = -ra`; never overflows |
| 9 | `MUL` | R3 | `mul rd, ra, rb` | exact product; faults if not 9-trit |
| 10 | `CMP` | R3 | `cmp rd, ra, rb` | `rd = -1`, `0` or `+1` by comparison |
| 11 | `SHL` | R3 | `shl rd, ra, rb` | `rd = ra * 3^amount`; amount is `rb` |
| 12 | `SHLI` | I4 | `shli rd, #i4` | `rd = rd * 3^i4` |
| 13 | `SHR` | R3 | `shr rd, ra, rb` | balanced right shift of `ra` by `rb` |
| 14 | `SHRI` | I4 | `shri rd, #i4` | balanced right shift of `rd` by `i4` |
| 15 | `LOAD` | M | `load rd, [ra]` | `rd = memory[ra]` |
| 16 | `STORE` | M | `store rd, [ra]` | `memory[ra] = rd` |
| 17 | `JMP` | J | `jmp ra` | `PC = ra` (absolute) |
| 18 | `BRZ` | B | `brz ra, target` | Branch if `ra == 0` |
| 19 | `BRNZ` | B | `brnz ra, target` | Branch if `ra != 0` |
| 20 | `BRN` | B | `brn ra, target` | Branch if `ra < 0` |
| 21 | `BRP` | B | `brp ra, target` | Branch if `ra > 0` |
| 22 | `TRAP` | I4/no-A | `trap #code` | Stop with an explicit trap code |
| 23 | `LI` | W | `li rd, #i9` | `rd = next word`; two-word instruction |
| 24 | `CMPI` | I4 | `cmpi rd, #i4` | `rd = compare(rd, i4)` |
| 25 | reserved | - | - | Illegal instruction |
| 26 | reserved | - | - | Illegal instruction |

`ADDI`, `SUBI`, `SHLI`, `SHRI` and `CMPI` use `rd` as both source and
destination.

## 5. Instruction semantics

All source operands are read before the destination is written, so an
instruction may use the same register more than once.

### 5.1 Arithmetic

Arithmetic is exact balanced-ternary arithmetic. Results that do not fit in 9
trits **fault**; they are not wrapped or saturated in ISA v0.

* `ADD`/`ADDI`: exact sum. If the 10th (carry) trit is nonzero, overflow fault.
* `SUB`/`SUBI`: exact difference. If the carry is nonzero, overflow fault.
* `NEG`: exact negation. The 9-trit range is symmetric, so this never faults.
* `MUL`: exact 18-trit product. If the high 9 trits are nonzero, overflow
  fault; otherwise the low 9 trits are written. There is no instruction to
  read the high half.
* `LDI`: converts the signed 4-trit immediate to a 9-trit word. Never faults.
* `CMP`: writes `-1` if `value(ra) < value(rb)`, `0` if equal, `+1` if
  `value(ra) > value(rb)`. Never faults.
* `CMPI`: compares the old value of `rd` with the sign-extended 4-trit
  immediate and writes `-1`, `0` or `+1` to `rd`. Never faults.

### 5.2 Shifts

The shift amount must be in `0..8`. A negative amount or an amount greater
than 8 is a shift-range fault. This applies to both register and immediate
forms.

* `SHL`/`SHLI`: exact multiplication by `3^amount`. If any nonzero trit would
  be shifted past position 8, overflow fault; otherwise the low 9 trits are
  written.
* `SHR`/`SHRI`: discard the low `amount` trits (balanced rounding, the same
  operation as `bt_shr` in `ternary.h`). This never overflows.

### 5.3 Memory

* `LOAD rd, [ra]`: address is `value(ra)`. If it is outside `0..728`, memory
  fault. Otherwise `rd = memory[address]`.
* `STORE rd, [ra]`: address is `value(ra)`. If it is outside `0..728`, memory
  fault. Otherwise `memory[address] = value(rd)`.
* If `rd` and `ra` are the same register, the address is computed from the old
  register value before the load/store takes effect.

### 5.4 Control flow

* `JMP ra`: `PC = value(ra)`. If the target is outside `0..728`, memory fault.
  Otherwise execution continues at that word.
* Branches are PC-relative. Let `next` be the address of the word after the
  branch instruction. For a branch at address `p`, `next = p + 1`. The target
  is `p + 1 + offset`, where `offset` is the signed 4-trit immediate.
* `BRZ` branches if the register value is zero; `BRNZ` if nonzero; `BRN` if
  negative; `BRP` if positive.
* If the condition is false, the branch is not taken and no target check is
  performed. If it is taken and the target is outside `0..728`, memory fault.
* `HALT` stops execution with status halted.
* `TRAP #code` stops execution with status trapped and the 4-trit code
  supplied by the instruction. Codes are user-defined; nonnegative codes
  `0..40` are recommended.

### 5.5 `LI`

`LI rd, #i9` is a two-word instruction:

1. Fetch and decode the `LI` word at `p`.
2. Fetch the literal word at `p + 1`. If `p + 1` is outside `0..728`, memory
   fault and `rd` is unchanged.
3. Write the literal word to `rd`.
4. Continue at `p + 2`.

The literal word is ordinary memory. There is no hardware check preventing a
branch or jump from targeting `p + 1`; if that happens, the literal is decoded
as an instruction. See the open questions.

## 6. Traps and faults

There is no trap handler, no trap vector, and no interrupt mechanism in v0. A
fault stops the machine with status trapped. It is reported to the host/CLI,
which may inspect registers and memory. `HALT` is not a fault: it stops with
status halted and no trap reason.

| Reason | Cause |
|---|---|
| `overflow` | `ADD`, `ADDI`, `SUB`, `SUBI`, `MUL`, `SHL` or `SHLI` result does not fit in 9 trits |
| `memory` | Fetch, load, store, jump or taken-branch target outside `0..728`; `LI` literal fetch outside memory |
| `illegal_instruction` | Reserved opcode 25/26, or a used register field encoding 8 |
| `shift_range` | Shift amount outside `0..8` |
| `explicit` | `TRAP #code` executed; carries the 4-trit code |

Fault atomicity: a faulting instruction performs **no** register write and no
memory write. For a taken branch with an invalid target, no branch is
performed. For `LI` with a missing literal, `rd` is unchanged. For `STORE`
with an invalid address, memory is unchanged.

When execution stops, the reference VM reports the address of the instruction
that caused the stop. For a fetch fault, it reports the attempted fetch
address. During normal execution the PC points at the next instruction to
fetch. Whenever the next fetch address becomes 729 (for example, after a
non-branch instruction at address 728), the next step reports a memory fault at
that attempted fetch before executing anything. A taken branch or `JMP` may of
course replace the PC with a valid target. The VM API and callback behavior are
defined in `vm.h`.

## 7. Reset and instruction fetch

On reset:

* `PC = 0`
* `r0..r7 = 0`
* status = running
* memory is zeroed on power-on initialization before the loader writes a
  program image; a CPU reset preserves memory (see `vm.h`)

One instruction step is:

1. If `PC` is outside `0..728`, memory fault at the attempted address.
2. Fetch the word at `PC`; increment `PC`.
3. Decode and validate the instruction fields.
4. Execute. `JMP` and taken branches replace `PC`; `LI` advances `PC` once
   more after fetching its literal; `HALT`/faults stop.
5. If no stop occurred, repeat.

After fetching address 728, the PC becomes 729 unless execution stops or a
taken branch/`JMP` replaces it. A step that executes a non-branch instruction at
address 728 can therefore return with status running and `PC = 729`; step 1
reports the fault on the next step. There is no instruction cache and no delay
slot. A memory write affects the next fetch of that word.

## 8. Assembly syntax (v0)

This is the syntax the assembler (`k3asm`) accepts. Besides the grammar below,
`k3asm` also accepts `//` as a comment and `#` at the start of a line as a
comment, so `#` can still be used as the immediate prefix. It additionally
supports the `.word N` directive.

```text
line        := [label ":"] [instruction] [comment]
label       := [A-Za-z_][A-Za-z0-9_]*
instruction := mnemonic [operand {"," operand}]
mnemonic    := NOP | HALT | MOV | LDI | ADD | ADDI | SUB | SUBI | NEG |
               MUL | CMP | SHL | SHLI | SHR | SHRI | LOAD | STORE |
               JMP | BRZ | BRNZ | BRN | BRP | TRAP | LI | CMPI
operand     := register | immediate | "[" register "]" | target
register    := "r" ("0".."7")
immediate   := "#" ["+"|"-"] decimal
target      := label | "#" ["+"|"-"] decimal
comment     := ";" { any character except newline }
```

* Mnemonics and register names are case-insensitive. Canonical spelling is
  uppercase mnemonic and lowercase `rN`.
* Identifiers are case-sensitive. A label denotes the address of the next
  emitted word.
* Whitespace and blank lines are ignored.
* A `LI` label points at the `LI` opcode, not at its literal word.
* `#i4` operands must fit `-40..+40`. `#i9` operands (only `LI`) must fit
  `-9841..+9841`. Branch `#offset` operands must fit `-40..+40`.
* The assembler should reject a `SHLI`/`SHRI` immediate outside `0..8`; the VM
  also checks it at run time.
* A label may be used as a branch target. For `LI`, an assembler may also
  accept a label where a full-word immediate is expected and substitute the
  label's address; this is an assembler convenience, not an ISA feature.
* Directives (`.word`, `.org`, data sections, etc.) are assembler-defined and
  are not part of ISA v0.

Example:

```asm
        ; r1 = 728
start:  ldi  r1, #1
        shli r1, #6          ; 1 * 3^6 = 729
        addi r1, #-1         ; 728

        ; count down from 5
        ldi  r2, #5
loop:   addi r2, #-1
        brnz r2, loop        ; offset -2 from this branch
        halt
```

Encoding examples (`t[0..8]`, least significant trit first):

```text
HALT                    t = [ 0, -1, -1,  0, 0,  0, 0,  0,  0 ]
ADD r3, r1, r2          t = [ 0,  0, -1, -1, 0,  0,-1, +1, -1 ]
LDI r1, #6              t = [-1,  0, -1,  0,-1,  0,-1, +1,  0 ]
BRNZ r1, #-2            t = [ 0, -1, +1,  0,-1, +1,-1,  0,  0 ]
```

## 9. Open design questions

These are **not** settled. The reference VM currently implements the v0 choice;
each can be changed before the ISA is frozen.

1. **Address encoding.** v0 uses the numerical value of a 9-trit word, valid
   `0..728`. Alternative: use six trits as unsigned base-3 digits for the
   address, or a biased six-trit value. The 9-trit form costs address range
   but avoids a second address encoding.
2. **Register field width.** v0 uses two trits per register and reserves
   encoding 8, allowing three register operands per word. Alternative: three
   trits per register with fewer operands per instruction.
3. **Instruction-field bias.** v0 treats opcode/register fields as base-3
   digits (`trit = digit - 1`) while data words are balanced. Alternative:
   use balanced values directly for fields, or a different field packing.
4. **Opcode space.** v0 reserves opcodes 25 and 26. Should they be assigned
   (e.g. `MULH`, `JMPI`) or left reserved?
5. **Overflow policy.** v0 faults and writes no result. Alternatives: wrap and
   expose a carry/status register; saturate; or make overflow a sticky flag.
6. **Multiply high half.** v0 traps when the product needs more than 9 trits
   and has no way to read the high half. Add `MULH`, or define `MUL` as
   wrapping?
7. **Shift amounts.** v0 accepts `0..8` and faults otherwise. Alternative:
   allow `>= 9` with the library's zero/overflow behavior, or take the amount
   modulo 9.
8. **Right-shift rounding.** v0 uses balanced dropping (round to nearest,
   ties impossible). Alternative: truncate toward zero or floor.
9. **Condition model.** v0 writes a comparison result to a GPR and branches on
   its sign. Alternative: condition-code flags set by ALU operations.
10. **Branch range.** v0 has a signed 4-trit offset, `-40..+40` words.
    Alternative: a register target, a 9-trit offset, or a two-word branch.
11. **Absolute jumps.** v0 only has `JMP ra`. Alternative: `JMP #i9` as a
    two-word absolute jump, or a direct branch-and-link.
12. **`LI` literal hazard.** v0 allows execution to branch into the literal
    word. Alternative: forbid labels/targets inside an `LI` pair, or add an
    explicit alignment/instruction-boundary rule.
13. **I/O.** v0 has no architectural I/O at all. Alternative: memory-mapped
    I/O, trap-based host calls, or a separate I/O instruction space. The VM's
    callback interface is not a program-visible I/O mechanism.
14. **Calls and stack.** v0 has no `CALL`/`RET` and no stack. Alternative: a
    link register, a software stack in memory, or a hardware stack.
15. **Trap handling.** v0 stops on a fault with no handler. Alternative: a
    trap vector, a handler mode, or a resumable exception model.
16. **Reset and loading.** v0 starts at address 0 with zeroed memory. Is
    address 0 always the entry point, or should there be a reset word/vector?
    What is the program-image format?
17. **Self-modifying code.** v0 permits it and has no instruction cache.
    Alternative: write-protect code or define a separate code/data split.
18. **Assembly literals.** v0 accepts signed decimal. Alternative: a balanced
    ternary literal syntax (for example, an explicit `+0-` form) and/or
    character/word literals.
19. **Register zero.** v0 makes all eight registers general. Alternative:
    hardwire `r0` to zero, which would simplify some instruction sequences but
    reduces the register file to seven usable registers.
20. **Trap atomicity.** v0 guarantees no register or memory write by a
    faulting instruction. Is that the intended architectural guarantee, or
    should partial progress be visible for debugging?

## 10. Verification status

* The reference VM core implements this ISA v0. Its tests execute every
  opcode, both reserved opcodes, used-register-field 8, every fault reason, the
  fetch-edge cases, callbacks, and the encoding examples in this document.
* The arithmetic semantics use the exhaustively tested `ternary.c` primitives.
  The VM trap and atomicity rules have separate tests.
* The assembler, CLI runner and example programs do not exist yet. The assembly
  syntax and program-image format remain unimplemented.
* The ISA remains a proposal; the open questions in section 9 are not settled.
