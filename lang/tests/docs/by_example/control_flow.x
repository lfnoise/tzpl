-- Control Flow from Tzopilotl by Example

-- If/else as expression
fn classify(n Int) String {
    if (n < 0) {
        "negative"
    } else if (n == 0) {
        "zero"
    } else {
        "positive"
    }
}
classify(-5) println;
classify(0) println;
classify(5) println;

-- Ternary expression
fn abs(x Int) Int = x < 0 ? -x : x;
abs(-10) println;
abs(10) println;

-- Ternary chained
fn clamp(x Int, lo Int, hi Int) Int = x < lo ? lo : x > hi ? hi : x;
clamp(-5, 0, 10) println;
clamp(5, 0, 10) println;
clamp(15, 0, 10) println;

-- While loop
var i = 0;
while (i < 5) {
    i println;
    i = i + 1;
}

-- Match
fn match_test(value Int) {
    match (value) {
        1: "one" println;
        2: "two" println;
        _: "other" println;
    }
}
match_test(1);
match_test(2);
match_test(3);

-- Match with guards
fn classify_guard(x Int) {
    match (x) {
        n if (n > 0): "positive" println;
        n if (n < 0): "negative" println;
        0: "zero" println;
    }
}
classify_guard(5);
classify_guard(-3);
classify_guard(0);
