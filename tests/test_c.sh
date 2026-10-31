#!/bin/sh
# Test suite for k3cc (K-Three C compiler).
#
# Each test compiles a small C program to K-Three assembly, assembles it with
# k3asm and runs it on k3vm, then checks the final machine state.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
K3CC=${K3CC:-"$ROOT/build/k3cc"}
K3ASM=${K3ASM:-"$ROOT/build/k3asm"}
K3VM=${K3VM:-"$ROOT/build/k3vm"}
TMP=${TMP:-"$ROOT/build/c-tests"}

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

# build NAME: compile $TMP/NAME.c and run it, leaving output in $out
build() {
    "$K3CC" "$TMP/$1.c" -o "$TMP/$1.s"
    "$K3ASM" "$TMP/$1.s" -o "$TMP/$1.k3"
    out=$("$K3VM" -d "$TMP/$1.k3")
}

expect_reg() {
    name=$1
    reg=$2
    want=$3
    if printf '%s\n' "$out" | grep -Fq "$reg = $want" && \
       printf '%s\n' "$out" | grep -Fq "status=halted"; then
        ok
    else
        bad "$name" "expected $reg = $want"
    fi
}

# Test 1: Recursive factorial
cat << 'EOF' > "$TMP/fact.c"
int fact(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}
int main() {
    int res = fact(5);
    return res;
}
EOF
build fact
expect_reg test_fact r0 120

# Test 2: While loop and arithmetic
cat << 'EOF' > "$TMP/loop.c"
int main() {
    int sum = 0;
    int i = 1;
    while (i <= 10) {
        sum = sum + i;
        i = i + 1;
    }
    return sum;
}
EOF
build loop
expect_reg test_loop r0 55

# Test 3: Multiple arguments and global variables
cat << 'EOF' > "$TMP/globals.c"
int g_val = 100;
int add3(int a, int b, int c) {
    return a + b + c + g_val;
}
int main() {
    int res = add3(5, 15, 30);
    return res;
}
EOF
build globals
expect_reg test_globals r0 150

# Test 4: for loop with initializer, condition and step
cat << 'EOF' > "$TMP/forloop.c"
int main() {
    int sum = 0;
    int i;
    for (i = 0; i < 10; i = i + 1) {
        sum = sum + i + 1;
    }
    return sum;
}
EOF
build forloop
expect_reg test_for r0 55

# Test 5: else-if chain
cat << 'EOF' > "$TMP/elseif.c"
int classify(int n) {
    if (n < 0) {
        return -1;
    } else if (n == 0) {
        return 0;
    } else {
        return 1;
    }
}
int main() {
    return classify(-5) + classify(0) * 10 + classify(7) * 100;
}
EOF
build elseif
expect_reg test_elseif r0 99

# Test 6: Nested calls inside expressions
cat << 'EOF' > "$TMP/nested.c"
int sq(int x) {
    return x * x;
}
int add(int a, int b) {
    return a + b;
}
int main() {
    return add(sq(3), sq(4)) + sq(add(1, 2));
}
EOF
build nested
expect_reg test_nested r0 34

# Test 7: print() writes the value to stdout
cat << 'EOF' > "$TMP/print.c"
int main() {
    print(42);
    return 0;
}
EOF
build print
if printf '%s\n' "$out" | grep -Fq "42"; then
    ok
else
    bad test_print "expected '42' on stdout"
fi

# Test 8: Branch distance larger than the 4-trit range of conditional jumps
cat << 'EOF' > "$TMP/longif.c"
int main() {
    int x = 0;
    if (x == 0) {
        x = x + 1; x = x + 1; x = x + 1; x = x + 1; x = x + 1;
        x = x + 1; x = x + 1; x = x + 1; x = x + 1; x = x + 1;
        x = x + 1; x = x + 1; x = x + 1; x = x + 1; x = x + 1;
        x = x + 1; x = x + 1; x = x + 1; x = x + 1; x = x + 1;
        x = x + 1; x = x + 1; x = x + 1; x = x + 1; x = x + 1;
        x = x + 1; x = x + 1; x = x + 1; x = x + 1; x = x + 1;
    }
    return x;
}
EOF
build longif
expect_reg test_long_branch r0 30

# Test 9: Globals declared between functions do not disturb the code stream
cat << 'EOF' > "$TMP/interleaved.c"
int first = 5;
int one() {
    return 1;
}
int second = 7;
int main() {
    return first + second + one();
}
EOF
build interleaved
expect_reg test_interleaved r0 13

# Test 10: division and modulo, including negative operands
cat << 'EOF' > "$TMP/divmod.c"
int main() {
    int a = 100;
    int b = 7;
    int q = a / b;
    int r = a % b;
    int nq = -100 / b;
    int nr = -100 % b;
    return q * 100 + r + nq * 100 + nr;
}
EOF
build divmod
expect_reg test_divmod r0 0

# Test 11: division by zero yields 0 instead of trapping
cat << 'EOF' > "$TMP/divzero.c"
int main() {
    int z = 0;
    return 5 / z + 5 % z;
}
EOF
build divzero
expect_reg test_divzero r0 0

# Test 12: tight operator spacing and unary minus
cat << 'EOF' > "$TMP/tight.c"
int main() {
    int x = 10;
    int y = -x + 3;
    return x-1 + y;
}
EOF
build tight
expect_reg test_tight r0 2

# Test 13: break and continue inside for loops
cat << 'EOF' > "$TMP/breakcont.c"
int main() {
    int sum = 0;
    int i;
    for (i = 1; i <= 20; i = i + 1) {
        if (i > 10) break;
        if (i % 2 == 0) continue;
        sum = sum + i;
    }
    return sum;
}
EOF
build breakcont
expect_reg test_break_continue r0 25

# Test 14: block-scoped locals are released (no stack leak) and nested loops
cat << 'EOF' > "$TMP/scope.c"
int main() {
    int i;
    int j;
    int n = 0;
    for (i = 0; i < 5; i = i + 1) {
        int a = i;
        for (j = 0; j < 5; j = j + 1) {
            int b = j;
            if (b == 2) continue;
            if (b == 4) break;
            n = n + 1;
        }
        if (a == 3) break;
    }
    return n;
}
EOF
build scope
expect_reg test_scope r0 12
if printf '%s\n' "$out" | grep -Fq "r7 = 6560"; then
    ok
else
    bad test_scope_stack "expected r7 = 6560 after balanced block scopes"
fi

# Test 16: local and global arrays
cat << 'EOF' > "$TMP/arrays.c"
int g[5];
int sum_arr() {
    int a[4];
    int i;
    int s = 0;
    for (i = 0; i < 4; i = i + 1) {
        a[i] = i * i;
    }
    for (i = 0; i < 4; i = i + 1) {
        s = s + a[i];
    }
    g[0] = s;
    g[1] = a[3];
    return g[0] + g[1];
}
int main() {
    return sum_arr();
}
EOF
build arrays
expect_reg test_arrays r0 23

# Test 17: bubble sort over a global array
cat << 'EOF' > "$TMP/sort.c"
int a[8];
int main() {
    int i;
    int j;
    int t;
    a[0] = 5; a[1] = 3; a[2] = 8; a[3] = 1;
    a[4] = 9; a[5] = 2; a[6] = 7; a[7] = 4;
    for (i = 0; i < 7; i = i + 1) {
        for (j = 0; j < 7 - i; j = j + 1) {
            if (a[j] > a[j + 1]) {
                t = a[j];
                a[j] = a[j + 1];
                a[j + 1] = t;
            }
        }
    }
    return a[0] * 100 + a[7];
}
EOF
build sort
expect_reg test_sort r0 109

# Test 18: array declared inside a loop body is released each iteration
cat << 'EOF' > "$TMP/arrscope.c"
int main() {
    int i;
    int total = 0;
    for (i = 0; i < 20; i = i + 1) {
        int a[8];
        a[0] = i;
        a[7] = i * 2;
        total = total + a[0] + a[7];
    }
    return total;
}
EOF
build arrscope
expect_reg test_array_scope r0 570
if printf '%s\n' "$out" | grep -Fq "r7 = 6560"; then
    ok
else
    bad test_array_scope_stack "expected r7 = 6560 after arrays in loop bodies"
fi

# Test 19: constant array initializers (local and global, zero-filled tail)
cat << 'EOF' > "$TMP/arrinit.c"
int g[5] = {1, -2, 3};
int main() {
    int a[4] = {10, 20};
    return a[0] + a[1] + a[2] + a[3] + g[0] + g[1] + g[2] + g[3] + g[4];
}
EOF
build arrinit
expect_reg test_array_init r0 32

# Test 20: invalid array initializers are rejected at compile time
cat << 'EOF' > "$TMP/arrinit_bad.c"
int main() {
    int a[2] = {1, 2, 3};
    return a[0];
}
EOF
if "$K3CC" "$TMP/arrinit_bad.c" -o "$TMP/arrinit_bad.s" 2>/dev/null; then
    bad test_array_init_overflow "expected too many initializers to fail"
else
    ok
fi

cat << 'EOF' > "$TMP/arrinit_bad2.c"
int main() {
    int x = 1;
    int a[2] = {x, 2};
    return a[0];
}
EOF
if "$K3CC" "$TMP/arrinit_bad2.c" -o "$TMP/arrinit_bad2.s" 2>/dev/null; then
    bad test_array_init_nonconst "expected non-constant initializer to fail"
else
    ok
fi

# Test 21: puts() emits null-terminated string literals with escapes
cat << 'EOF' > "$TMP/puts.c"
int main() {
    puts("Hello, K-Three!\n");
    puts("quote: \" slash: \\\n");
    return 0;
}
EOF
build puts
if printf '%s\n' "$out" | grep -Fq 'Hello, K-Three!' && \
   printf '%s\n' "$out" | grep -Fq 'quote: " slash: \'; then
    ok
else
    bad test_puts "expected decoded string output"
fi

# Test 22: puts() rejects non-string arguments
cat << 'EOF' > "$TMP/puts_bad.c"
int main() {
    puts(5);
    return 0;
}
EOF
if "$K3CC" "$TMP/puts_bad.c" -o "$TMP/puts_bad.s" 2>/dev/null; then
    bad test_puts_bad "expected puts(5) to fail"
else
    ok
fi

# Test 23: character literals with escapes
cat << 'EOF' > "$TMP/chars.c"
int main() {
    putc('A');
    putc('\n');
    putc('\\');
    putc('\'');
    putc('\n');
    print('z');
    return 0;
}
EOF
build chars
if printf '%s\n' "$out" | grep -Fq 'A' && \
   printf '%s\n' "$out" | grep -Fq "\'" && \
   printf '%s\n' "$out" | grep -Fq '122'; then
    ok
else
    bad test_chars "expected character literal output"
fi

# Test 24: empty character literals are rejected
cat << 'EOF' > "$TMP/chars_bad.c"
int main() {
    return '';
}
EOF
if "$K3CC" "$TMP/chars_bad.c" -o "$TMP/chars_bad.s" 2>/dev/null; then
    bad test_chars_bad "expected empty character literal to fail"
else
    ok
fi

# Test 25: logical operators short-circuit and yield 0/1
cat << 'EOF' > "$TMP/logic.c"
int boom() {
    print(999);
    return 1;
}
int main() {
    int a = 0;
    int b = 5;
    int r = 0;
    if (a || b) { r = r + 1; }
    if (a && b) { r = r + 100; }
    if (!a && !(b == 4)) { r = r + 10; }
    if (a != 0 && (b / a) > 0) { r = r + 100; }
    if (0 && boom()) { r = r + 100; }
    if (1 || boom()) { r = r + 1; }
    if (1 || 0 && 0) { r = r + 1; }
    if ((1 || 0) && 0) { r = r + 100; }
    return r;
}
EOF
build logic
expect_reg test_logic r0 13
if printf '%s\n' "$out" | grep -Fq '999'; then
    bad test_logic_shortcircuit "boom() was evaluated despite short-circuit"
else
    ok
fi

# Test 26: do/while loops with break/continue and the ternary operator
cat << 'EOF' > "$TMP/do.c"
int absv(int x) {
    return x < 0 ? -x : x;
}
int main() {
    int i = 0;
    int s = 0;
    do {
        i = i + 1;
        if (i == 3) { continue; }
        if (i > 5) { break; }
        s = s + i;
    } while (i < 10);
    return s * 100 + absv(-7) + absv(4);
}
EOF
build do
expect_reg test_do_ternary r0 1211

printf 'test_c: %d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
