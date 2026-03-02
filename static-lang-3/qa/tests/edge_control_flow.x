-- QA: Edge cases in control flow

-- If/else as expression
let x = if (true) { 1 } else { 2 };
x println;

let y = if (false) { 1 } else { 2 };
y println;

-- Nested if/else as expression
fn grade(score Int) String {
    if (score >= 90) { "A" }
    else if (score >= 80) { "B" }
    else if (score >= 70) { "C" }
    else if (score >= 60) { "D" }
    else { "F" }
}
grade(95) println;
grade(85) println;
grade(75) println;
grade(65) println;
grade(55) println;

-- While loop with break
var count = 0;
while (true) {
    if (count >= 5) { break; }
    count = count + 1;
}
count println;

-- While loop with continue
var sum = 0;
var i = 0;
while (i < 10) {
    i = i + 1;
    if (i % 2 == 0) { continue; }
    sum = sum + i;
}
sum println;  -- 1 + 3 + 5 + 7 + 9 = 25

-- For loop with break
var total = 0;
for (n : (1..100)) {
    if (n > 5) { break; }
    total = total + n;
}
total println;  -- 1 + 2 + 3 + 4 + 5 = 15

-- For loop with continue
var even_sum = 0;
for (n : (1..10)) {
    if (n % 2 != 0) { continue; }
    even_sum = even_sum + n;
}
even_sum println;  -- 2 + 4 + 6 + 8 + 10 = 30

-- Nested loops with break (inner only)
var result = 0;
for (i2 : (1..3)) {
    for (j : (1..5)) {
        if (j > 2) { break; }
        result = result + i2 * 10 + j;
    }
}
result println;  -- 11+12 + 21+22 + 31+32 = 129

-- Early return from function
fn find_first_even(arr [Int]) Int {
    for (x : arr) {
        if (x % 2 == 0) { return x; }
    }
    return -1;
}
find_first_even([1, 3, 5, 4, 7]) println;  -- 4
find_first_even([1, 3, 5, 7]) println;     -- -1

-- Ternary chaining
fn fizzbuzz(n Int) String {
    n % 15 == 0 ? "FizzBuzz" :
    n % 3 == 0  ? "Fizz" :
    n % 5 == 0  ? "Buzz" :
    fmt("%^", n)
}
fizzbuzz(15) println;
fizzbuzz(9) println;
fizzbuzz(10) println;
fizzbuzz(7) println;

-- While loop that doesn't execute
var never_ran = true;
while (false) {
    never_ran = false;
}
never_ran println;

-- For loop over empty array
let empty [Int] = [];
var for_ran = false;
for (x : empty) {
    for_ran = true;
}
for_ran println;

-- Match with fallthrough to wildcard
fn test_match(x Int) String {
    match (x) {
        1: return "one";
        2: return "two";
        _: return "other";
    }
    "unreachable"
}
test_match(1) println;
test_match(2) println;
test_match(999) println;

-- Infinite range with break
var inf_sum = 0;
for (n : (1..)) {
    if (n > 10) { break; }
    inf_sum = inf_sum + n;
}
inf_sum println;  -- 55
