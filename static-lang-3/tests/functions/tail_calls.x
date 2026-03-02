-- Tail call optimization tests

-- Tail-recursive factorial (accumulator pattern)
fn fact(n Int, acc Int) Int {
    if (n <= 1) { return acc; }
    fact(n - 1, n * acc)
}
fact(1, 1) println;
fact(5, 1) println;
fact(10, 1) println;
fact(20, 1) println;

-- Tail-recursive sum
fn sum_to(n Int, acc Int) Int {
    if (n == 0) { return acc; }
    sum_to(n - 1, acc + n)
}
sum_to(0, 0) println;
sum_to(10, 0) println;
sum_to(100, 0) println;

-- Deep tail recursion (would overflow without TCO)
fn countdown(n Int) Int {
    if (n == 0) { return 0; }
    countdown(n - 1)
}
countdown(100000) println;

-- Mutual recursion via tail calls
fn is_even(n Int) Bool {
    if (n == 0) { return true; }
    is_odd(n - 1)
}
fn is_odd(n Int) Bool {
    if (n == 0) { return false; }
    is_even(n - 1)
}
is_even(0) println;
is_even(4) println;
is_odd(3) println;
is_odd(4) println;

-- Tail call with explicit return
fn sum_explicit(n Int, acc Int) Int {
    if (n == 0) { return acc; }
    return sum_explicit(n - 1, acc + n);
}
sum_explicit(10, 0) println;
