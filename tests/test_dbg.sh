#!/bin/sh
# Scripted command tests for k3dbg.
#
# Tool paths default to the build directory and can be overridden:
#   K3ASM=... K3DBG=... sh tests/test_dbg.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
K3ASM=${K3ASM:-"$ROOT/build/k3asm"}
K3DIS=${K3DIS:-"$ROOT/build/k3dis"}
K3DBG=${K3DBG:-"$ROOT/build/k3dbg"}
TMP=${TMP:-"$ROOT/build/dbg-tests"}

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

# assemble NAME
assemble() {
    name=$1
    if ! "$K3ASM" "$ROOT/examples/$name.s" -o "$TMP/$name.k3" \
            2>"$TMP/$name.err"; then
        printf 'test_dbg: cannot assemble %s: %s\n' "$name" \
            "$(cat "$TMP/$name.err")" >&2
        exit 1
    fi
}

# run NAME OPTIONS COMMANDS
# Feed COMMANDS (with \n escapes) to k3dbg and print its stdout.
run() {
    name=$1
    options=$2
    commands=$3

    # Word splitting is intentional: OPTIONS is a short option list.
    printf '%b' "$commands" | "$K3DBG" -q $options "$TMP/$name.k3"
}

# check NAME OUTPUT EXPECTED_SUBSTRING
check() {
    name=$1
    output=$2
    expected=$3

    if printf '%s\n' "$output" | grep -Fq -- "$expected"; then
        ok
    else
        bad "$name" "expected '$expected' in: $(printf '%s' "$output" | tr '\n' '|')"
    fi
}

assemble counter
assemble memtest
assemble overflow

# Initial state.
out=$(run counter '' 'regs\nquit\n')
check initial_pc "$out" 'pc = 0'
check initial_status "$out" 'status = running'
check initial_r0 "$out" 'r0 = 0'

# Stepping advances the pc and register state.
out=$(run counter '' 'step 2\nreg r0\nquit\n')
check step_pc "$out" 'pc=0002  BRNZ r0, -2'
check step_r0 "$out" 'r0 = 2'

# Continue runs to HALT.
out=$(run counter '' 'continue\nreg r0\nquit\n')
check continue_halt "$out" 'status=halted'
check continue_r0 "$out" 'r0 = 0'

# A breakpoint stops before the instruction at that address.
out=$(run counter '' 'break 3\ncontinue\nregs\nstep\nquit\n')
check break_set "$out" 'breakpoint at 3'
check break_pc "$out" 'pc = 3'
check break_running "$out" 'status = running'
check break_step_halt "$out" 'status=halted pc=3'

# Memory writes are visible after the program stops.
out=$(run memtest '' 'continue\nmem 10 1\nreg r3\nquit\n')
check memtest_halt "$out" 'status=halted'
check memtest_value "$out" 'mem[10] = 123'
check memtest_compare "$out" 'r3 = 0'

# Traps are reported with their reason and leave registers unchanged.
out=$(run overflow '' 'continue\nreg r0\nquit\n')
check trap_status "$out" 'status=trapped'
check trap_reason "$out" 'reason=overflow'
check trap_r0 "$out" 'r0 = 9841'

# Register, memory and pc changes.
out=$(run counter '' 'set r0 5\nreg r0\nsetmem 20 -9841\nmem 20 1\nset pc 10\nregs\nquit\n')
check set_reg "$out" 'r0 = 5'
check set_mem "$out" 'mem[20] = -9841'
check set_pc "$out" 'pc = 10'

# Labeled disassembly marks the branch target.  LI has a two-word encoding,
# so the showing of overflow's literal must be skipped.
out=$(run counter '-l' 'dis 0 4\nquit\n')
check labels_target "$out" 'L0001:'
check labels_ldi "$out" 'LDI r0, #3'
check labels_branch "$out" 'BRNZ r0, -2'

out=$(run overflow '' 'dis 0 4\nquit\n')
check dis_li "$out" 'LI r0, #9841'
check dis_literal_skipped "$out" '0002: ADDI r0, #1'

# The continue step limit leaves the VM runnable and reports the count.
out=$(run counter '-s 2' 'continue\nquit\n')
check step_limit "$out" 'step limit reached after 2 instructions'

# Unknown commands are diagnosed but do not end the session.
out=$(run counter '' 'bogus\nquit\n')
check unknown_command "$out" "error: unknown command 'bogus'"

# Info and breakpoint deletion.
out=$(run counter '' 'break 2\ninfo\ndelete 2\ninfo\nquit\n')
check info_breakpoint "$out" 'breakpoints: 2'
check delete_breakpoint "$out" 'breakpoint cleared at 2'
check info_no_breakpoint "$out" 'breakpoints: none'

# Reset restores the loaded image and clears registers.
out=$(run counter '' 'step 2\nreset\nregs\nquit\n')
check reset_pc "$out" 'pc = 0'
check reset_r0 "$out" 'r0 = 0'

# Help is available.
out=$(run counter '' 'help\nquit\n')
check help "$out" 'step [N]'
check help_save "$out" 'save PATH'

# Save memory image as text and binary.
out=$(run counter '' "setmem 50 1234\nsave $TMP/dump.k3 text\nsave $TMP/dump.k3b bin\nquit\n")
check save_text "$out" "saved $TMP/dump.k3"
check save_bin "$out" "saved $TMP/dump.k3b"
if grep -Fxq '1234' "$TMP/dump.k3" && \
   "$K3DIS" "$TMP/dump.k3b" >/dev/null 2>&1; then
    ok
else
    bad save_files "saved image files are missing or invalid"
fi

# A missing image is a load error with exit status 1.
if "$K3DBG" -q "$TMP/does-not-exist.k3" >/dev/null \
        2>"$TMP/missing.err"; then
    bad load_error 'k3dbg accepted a missing image'
else
    if grep -Fq 'No such file' "$TMP/missing.err"; then
        ok
    else
        bad load_error "unexpected diagnostic: $(tr '\n' '|' <"$TMP/missing.err")"
    fi
fi

printf 'test_dbg: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
