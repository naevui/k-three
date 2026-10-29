// Simple C test program for K-Three
int fact(int n) {
    if (n <= 1) {
        return 1;
    }
    return n * fact(n - 1);
}

int main() {
    int res = fact(5); // 120
    print(res);
    return res;
}
