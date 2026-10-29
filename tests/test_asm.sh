#!/bin/sh
# Assembler encoding, syntax and error-handling tests.
#
# Tool paths default to the build directory and can be overridden:
#   K3ASM=... K3DIS=... sh tests/test_asm.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
K3ASM=${K3ASM:-"$ROOT/build/k3asm"}
K3DIS=${K3DIS:-"$ROOT/build/k3dis"}
TMP=${TMP:-"$ROOT/build/asm-tests"}

rm -rf "$TMP"
mkdir -p "$TMP"

pass=0
fail=0

ok() {
    pass=$((pass + 1))
}

bad() {
    printf 'FAIL: %s: %s\n' "$1" "$2" >&2
    fail=$((fail + 1))
}

# encode NAME SOURCE EXPECTED_LINE
# Assemble SOURCE and require that the disassembly contains EXPECTED_LINE.
encode() {
    name=$1
    source=$2
    expected=$3
    printf '%s\n' "$source" >"$TMP/$name.s"
    if ! "$K3ASM" "$TMP/$name.s" -o "$TMP/$name.k3" 2>"$TMP/$name.err"; then
        bad "$name" "assembly failed: $(cat "$TMP/$name.err")"
        return
    fi
    if "$K3DIS" -t "$TMP/$name.k3" | grep -Fq "$expected"; then
        ok
    else
        bad "$name" "expected '$expected' in: $("$K3DIS" -t "$TMP/$name.k3" | tr '\n' '|')"
    fi
}

# reject NAME SOURCE EXPECTED_MESSAGE
reject() {
    name=$1
    source=$2
    expected=$3
    printf '%s\n' "$source" >"$TMP/$name.s"
    rm -f "$TMP/$name.k3"
    if "$K3ASM" "$TMP/$name.s" -o "$TMP/$name.k3" 2>"$TMP/$name.err"; then
        bad "$name" "assembler accepted invalid input"
        return
    fi
    if [ -e "$TMP/$name.k3" ]; then
        bad "$name" "output file was written despite errors"
        return
    fi
    if grep -Fq "$expected" "$TMP/$name.err"; then
        ok
    else
        bad "$name" "expected error '$expected', got: $(tr '\n' '|' <"$TMP/$name.err")"
    fi
}

# The four encodings listed in ISA.md section 8, trit for trit.
encode isa_halt 'halt' 'HALT  t=[0--000000]'
encode isa_add 'ADD r3, r1, r2' 'ADD r3, r1, r2  t=[00--00-+-]'
encode isa_ldi 'LDI r1, #6' 'LDI r1, #6  t=[-0-0-0-+0]'
encode isa_brnz 'BRNZ r1, #-2' 'BRNZ r1, -2  t=[0-+0-+-00]'

# A '#' immediate must not be treated as a comment (regression test).
encode hash_immediate 'addi r0, #-1' 'ADDI r0, #-1'
encode hash_line_comment '# whole line comment
halt' 'HALT'
encode semicolon_comment 'addi r0, #3 ; trailing comment' 'ADDI r0, #3'
encode slash_comment 'addi r0, #3 // trailing comment' 'ADDI r0, #3'

# Case-insensitive mnemonics and registers.
encode lowercase 'lDi R1, #-3' 'LDI r1, #-3'

# Forward label resolution and LI's two-word size.  `end` is at address 3
# because LI occupies words 1 and 2, so the branch offset from word 0 is 2.
encode forward_label 'brnz r0, end
li r0, #123
end: halt' 'BRNZ r0, 2'
encode li_literal 'li r0, #-9841' 'LI r0, #-9841'

# Labeled disassembly marks the resolved branch target (LI is two words).
if "$K3DIS" -l "$TMP/forward_label.k3" | grep -Fq 'L0003:'; then
    ok
else
    bad labels "expected the branch target label L0003"
fi

# The .word directive emits raw data, so inspect the words, not a decode.
printf '.word -123\n' >"$TMP/dot_word.s"
if "$K3ASM" "$TMP/dot_word.s" -o "$TMP/dot_word.k3" 2>"$TMP/dot_word.err" &&
   "$K3DIS" -w "$TMP/dot_word.k3" | grep -Fq '0000: -123'; then
    ok
else
    bad dot_word "expected the raw word -123"
fi

# Labels may share a line with an instruction.
encode label_same_line 'start: ldi r0, #1
brnz r0, start' 'BRNZ r0, -2'

# Duplicate labels and malformed input are rejected.
reject reserved_register 'mov r8, r1' "invalid register 'r8'"
reject immediate_range 'ldi r0, #41' 'does not fit in 4 trits'
reject negative_immediate_range 'ldi r0, #-41' 'does not fit in 4 trits'
reject shift_range 'shli r0, #9' 'must be 0..8'
reject unknown_label 'brz r0, nowhere' "unknown label 'nowhere'"
reject unknown_mnemonic 'frobnicate r0' "unknown mnemonic 'FROBNICATE'"
reject wrong_operands 'add r0, r1' 'expects 3 operands'
reject duplicate_label 'a: halt
a: halt' "duplicate label 'a'"
reject literal_range 'li r0, #9842' 'does not fit in 9 trits'

printf 'test_asm: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
