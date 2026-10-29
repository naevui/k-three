#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
K3ASM=${K3ASM:-"$ROOT/build/k3asm"}
K3VM=${K3VM:-"$ROOT/build/k3vm"}
K3DIS=${K3DIS:-"$ROOT/build/k3dis"}
TMP=${TMP:-"$ROOT/build/asm-ext-tests"}

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

# Test 1: Character literals and ternary literals
cat << 'EOF' > "$TMP/literals.s"
    li r0, #'A'
    ldi r1, #'\n'
    ldi r2, #0t+0-
    halt
EOF
"$K3ASM" "$TMP/literals.s" -o "$TMP/literals.k3"
out=$("$K3DIS" "$TMP/literals.k3")
if printf '%s\n' "$out" | grep -Fq "LI r0, #65" && \
   printf '%s\n' "$out" | grep -Fq "LDI r1, #10" && \
   printf '%s\n' "$out" | grep -Fq "LDI r2, #8"; then
    ok
else
    bad literals "failed to parse character or ternary literals"
fi

# Test 2: .equ directive
cat << 'EOF' > "$TMP/equ_test.s"
.equ MY_VAL, 33
.equ CHAR_X, 'X'
    ldi r0, #MY_VAL
    li r1, #CHAR_X
    halt
EOF
"$K3ASM" "$TMP/equ_test.s" -o "$TMP/equ_test.k3"
out=$("$K3DIS" "$TMP/equ_test.k3")
if printf '%s\n' "$out" | grep -Fq "LDI r0, #33" && \
   printf '%s\n' "$out" | grep -Fq "LI r1, #88"; then
    ok
else
    bad equ_test "failed to evaluate .equ constants"
fi

# Test 3: Pseudoinstructions CLR, INC, DEC, PUSH, POP, RET
cat << 'EOF' > "$TMP/pseudo.s"
    clr r0
    inc r0
    dec r0
    push r0
    pop r1
    ret
EOF
"$K3ASM" "$TMP/pseudo.s" -o "$TMP/pseudo.k3"
out=$("$K3DIS" "$TMP/pseudo.k3")
if printf '%s\n' "$out" | grep -Fq "LDI r0, #0" && \
   printf '%s\n' "$out" | grep -Fq "ADDI r0, #1" && \
   printf '%s\n' "$out" | grep -Fq "ADDI r0, #-1" && \
   printf '%s\n' "$out" | grep -Fq "STORE r0, [r7]" && \
   printf '%s\n' "$out" | grep -Fq "LOAD r1, [r7]" && \
   printf '%s\n' "$out" | grep -Fq "JMP r6"; then
    ok
else
    bad pseudo "failed to expand pseudoinstructions"
fi

# Test 4: .string and .space
cat << 'EOF' > "$TMP/data.s"
msg:
    .string "Hi"
buf:
    .space 3
    halt
EOF
"$K3ASM" "$TMP/data.s" -o "$TMP/data.k3"
words=$("$K3DIS" -w "$TMP/data.k3")
# 'H' is 72, 'i' is 105, 0 terminator, then 3 zeroes
if printf '%s\n' "$words" | grep -Fq "0000: 72" && \
   printf '%s\n' "$words" | grep -Fq "0001: 105" && \
   printf '%s\n' "$words" | grep -Fq "0002: 0" && \
   printf '%s\n' "$words" | grep -Fq "0003: 0" && \
   printf '%s\n' "$words" | grep -Fq "0005: 0"; then
    ok
else
    bad data "failed to emit string and space"
fi

# Test 5: .include
cat << 'EOF' > "$TMP/inc_header.s"
.equ GREETING_LEN, 5
EOF

cat << 'EOF' > "$TMP/main.s"
.include "inc_header.s"
    ldi r0, #GREETING_LEN
    halt
EOF
"$K3ASM" "$TMP/main.s" -o "$TMP/main.k3"
out=$("$K3DIS" "$TMP/main.k3")
if printf '%s\n' "$out" | grep -Fq "LDI r0, #5"; then
    ok
else
    bad include "failed to process .include directive"
fi

# Test 6: compact binary .k3b output, magic, and auto-detection
cat << 'EOF' > "$TMP/binary.s"
    ldi r0, #7
    ldi r1, #1
loop:
    mul r1, r1, r0
    addi r0, #-1
    brnz r0, loop
    halt
EOF
"$K3ASM" -b "$TMP/binary.s" -o "$TMP/binary.k3b"
magic=$(od -An -tx1 -N4 "$TMP/binary.k3b" | tr -d ' \n')
if [ "$magic" = "7f4b3342" ] && \
   "$K3DIS" "$TMP/binary.k3b" | grep -Fq "MUL r1, r1, r0" && \
   "$K3VM" -d "$TMP/binary.k3b" | grep -Fq "r1 = 5040"; then
    ok
else
    bad binary "failed to write or read a compact binary image"
fi

# Test 7: -b picks a .k3b default output name
cp "$TMP/binary.s" "$TMP/bin2.s"
rm -f "$TMP/bin2.k3b"
( cd "$TMP" && "$K3ASM" -b bin2.s >/dev/null )
if [ -s "$TMP/bin2.k3b" ]; then
    ok
else
    bad binary_default "missing default .k3b output"
fi

printf 'test_asm_extended: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
