-- If expressions (if/else as expressions returning values)

-- Basic if-else expression in function
fn abs_val(x Int) Int {
    if (x < 0) { -x } else { x }
}

-7 abs_val println;
7 abs_val println;
0 abs_val println;

-- If-else if-else chain
fn classify(n Int) String {
    if (n < 0) {
        "negative"
    } else if (n == 0) {
        "zero"
    } else if (n < 10) {
        "small"
    } else {
        "big"
    }
}

-5 classify println;
0 classify println;
7 classify println;
42 classify println;

-- Nested if expressions
fn nested_test(a Bool, b Bool) Int {
    if (a) {
        if (b) { 1 } else { 2 }
    } else {
        if (b) { 3 } else { 4 }
    }
}

nested_test(true, true) println;
nested_test(true, false) println;
nested_test(false, true) println;
nested_test(false, false) println;
