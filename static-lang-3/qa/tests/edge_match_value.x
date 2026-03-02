-- QA: Match as expression / value edge cases

-- Match in function return (works)
fn classify(n Int) String {
    match (n) {
        0: return "zero";
        x if (x > 0): return "positive";
        _: return "negative";
    }
    ""
}
classify(0) println;
classify(42) println;
classify(-5) println;

-- Match on string
fn string_match(s String) Int {
    match (s) {
        "hello": return 1;
        "world": return 2;
        _: return 0;
    }
    0
}
string_match("hello") println;
string_match("world") println;
string_match("other") println;

-- Match on integer literals
fn day_name(d Int) String {
    match (d) {
        1: return "Monday";
        2: return "Tuesday";
        3: return "Wednesday";
        4: return "Thursday";
        5: return "Friday";
        6: return "Saturday";
        7: return "Sunday";
        _: return "Unknown";
    }
    ""
}
day_name(1) println;
day_name(5) println;
day_name(7) println;
day_name(99) println;

-- Match with binding and guard
fn abs_val(x Int) Int {
    match (x) {
        n if (n >= 0): return n;
        n: return -n;
    }
    0
}
abs_val(42) println;
abs_val(-42) println;
abs_val(0) println;

-- Match on enum with multiple patterns
enum Color { red, green, blue }
fn is_primary(c Color) Bool {
    match (c) {
        Color.red: return true;
        Color.blue: return true;
        _: return false;
    }
    false
}
is_primary(Color.red) println;
is_primary(Color.green) println;
is_primary(Color.blue) println;

-- Nested match
fn nested_match(x Int, y Int) String {
    match (x) {
        0: {
            match (y) {
                0: return "origin";
                _: return "y-axis";
            }
        }
        _: {
            match (y) {
                0: return "x-axis";
                _: return "plane";
            }
        }
    }
    ""
}
nested_match(0, 0) println;
nested_match(0, 5) println;
nested_match(3, 0) println;
nested_match(3, 5) println;
