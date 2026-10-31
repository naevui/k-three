/*
 * K-Three C demo - showcases the k3cc dialect.
 *
 * Build and run:
 *   build/k3cc examples/c_demo.c -o build/c_demo.s
 *   build/k3asm build/c_demo.s -o build/c_demo.k3
 *   build/k3vm build/c_demo.k3
 */
int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    puts("K-Three C demo\n");

    int primes[5] = {2, 3, 5, 7, 11};
    int sum = 0;
    int i;
    for (i = 0; i < 5; i++) {
        sum += primes[i];
    }
    print(sum);

    switch (fib(7)) {
    case 13:
        puts("fib(7) = 13\n");
        break;
    default:
        puts("unexpected\n");
    }

    int x = 0;
    do {
        x += 2;
    } while (x < 10);
    print(x > 5 ? x * 10 : -1);

    return sum + fib(7);
}
