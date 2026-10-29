#!/bin/sh
# Assemble and run every example, checking exit status and architectural state.
#
# Tool paths default to the build directory and can be overridden:
#   K3ASM=... K3VM=... sh tests/test_examples.sh

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
K3ASM=${K3ASM:-"$ROOT/build/k3asm"}
K3VM=${K3VM:-"$ROOT/build/k3vm"}
OUT=${OUT:-"$ROOT/build/examples"}

mkdir -p "$OUT"

pass=0
fail=0

fail_with() {
    printf 'FAIL: %s: %s\n' "$1" "$2" >&2
    fail=$((fail + 1))
}

good() {
    pass=$((pass + 1))
}

# run NAME
# Sets RUN_OUT (stdout of the VM) and RUN_RC (exit status).
run() {
    name=$1
    set +e
    RUN_OUT=$("$K3VM" -d -m "$OUT/$name.k3" 2>"$OUT/$name.err")
    RUN_RC=$?
    set -e
}

expect() {
    name=$1
    text=$2
    if ! printf '%s\n' "$RUN_OUT" | grep -Fqx "$text"; then
        fail_with "$name" "missing line '$text' (exit $RUN_RC)"
        return 1
    fi
    return 0
}

expect_match() {
    name=$1
    text=$2
    if ! printf '%s\n' "$RUN_OUT" | grep -Fq "$text"; then
        fail_with "$name" "missing '$text' (exit $RUN_RC)"
        return 1
    fi
    return 0
}

expect_rc() {
    name=$1
    want=$2
    if [ "$RUN_RC" -ne "$want" ]; then
        fail_with "$name" "exit status $RUN_RC, expected $want"
        return 1
    fi
    return 0
}

for src in "$ROOT"/examples/*.s; do
    name=$(basename "$src" .s)
    if ! "$K3ASM" "$src" -o "$OUT/$name.k3"; then
        fail_with "$name" "assembly failed"
    fi
done

# counter: r0 counts down to 0 and halts.
run counter
if expect counter "r0 = 0" && expect_rc counter 0; then good; fi

# factorial: 7! = 5040.
run factorial
if expect factorial "r0 = 0" && expect factorial "r1 = 5040" && \
   expect_rc factorial 0; then
    good
fi

# memtest: the stored value survives the round trip.
run memtest
if expect memtest "r1 = 123" && expect memtest "r2 = 123" && \
   expect memtest "r3 = 0" && expect memtest "mem[10] = 123" && \
   expect_rc memtest 0; then
    good
fi

# loop_label: sum 1..6 = 21.
run loop_label
if expect loop_label "r0 = 0" && expect loop_label "r1 = 21" && \
   expect_rc loop_label 0; then
    good
fi

# isa_example: r1 = 728, r2 = 0.
run isa_example
if expect isa_example "r1 = 728" && expect isa_example "r2 = 0" && \
   expect_rc isa_example 0; then
    good
fi

# overflow: traps with reason overflow and leaves r0 at 9841.
run overflow
if expect overflow "r0 = 9841" && expect_rc overflow 2 && \
   expect_match overflow "status=trapped" && \
   expect_match overflow "reason=overflow"; then
    good
fi

# syscall_demo: runs and exits cleanly with rc 0.
run syscall_demo
if expect_match syscall_demo "K-Three Syscall & OS Interface Demo" && \
   expect_match syscall_demo "Total memory words: 2187" && \
   expect_rc syscall_demo 0; then
    good
fi

# k3os: interactive monitor boots, handles EOF and halts cleanly with rc 0.
run k3os
if expect_match k3os "K3-OS" && \
   expect_match k3os "Bye!" && \
   expect_rc k3os 0; then
    good
fi

# snake: real-time game boots, handles 'q' and halts cleanly with rc 0.
printf 'q\n' > "$OUT/snake.in"
RUN_OUT=$("$K3VM" "$OUT/snake.k3" < "$OUT/snake.in" 2>"$OUT/snake.err")
RUN_RC=$?
if expect_match snake "K3 TERNARY SNAKE" && \
   expect_match snake "GAME OVER" && \
   expect_rc snake 0; then
    good
fi

# The documented step limit exits with status 3.
set +e
"$K3VM" -q -s 1 "$OUT/counter.k3" >/dev/null 2>&1
limit_rc=$?
set -e
if [ "$limit_rc" -eq 3 ]; then
    good
else
    fail_with counter "step limit exit status $limit_rc, expected 3"
fi

# -t traces decoded instructions to stderr; stdout stays quiet with -q.
set +e
"$K3VM" -q -t -s 2 "$OUT/counter.k3" >"$OUT/trace.out" 2>"$OUT/trace.err"
trace_rc=$?
set -e
if [ "$trace_rc" -eq 3 ] &&
   [ ! -s "$OUT/trace.out" ] &&
   grep -Fq 'pc=0000  LDI r0, #3' "$OUT/trace.err" &&
   grep -Fq 'pc=0001  ADDI r0, #-1' "$OUT/trace.err"; then
    good
else
    fail_with trace "unexpected decoded trace (exit $trace_rc)"
fi

printf 'test_examples: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
