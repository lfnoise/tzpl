// Recursive Fibonacci. Exercises function-call overhead.
#include <cstdio>

static long long fib(long long n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    std::printf("%lld\n", fib(38));
}
