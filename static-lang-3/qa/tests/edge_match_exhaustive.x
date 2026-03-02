-- QA: What happens with non-exhaustive match?

enum Color { red, green, blue }

-- Exhaustive match - should work fine
fn show_color(c Color) String {
    match (c) {
        Color.red: return "RED";
        Color.green: return "GREEN";
        Color.blue: return "BLUE";
    }
    "unreachable"
}
show_color(Color.red) println;
show_color(Color.green) println;
show_color(Color.blue) println;

-- Non-exhaustive match - what happens at runtime?
fn partial_match(c Color) String {
    match (c) {
        Color.red: return "RED";
        Color.green: return "GREEN";
        -- missing blue!
    }
    "no match"  -- fallthrough
}
partial_match(Color.red) println;
partial_match(Color.green) println;
partial_match(Color.blue) println;  -- should fall through to "no match"

-- Match on Int without wildcard
fn int_match(x Int) String {
    match (x) {
        1: return "one";
        2: return "two";
    }
    "no match"
}
int_match(1) println;
int_match(2) println;
int_match(3) println;  -- no match

-- Match with only wildcard
fn any_match(x Int) String {
    match (x) {
        _: return "matched";
    }
    "unreachable"
}
any_match(42) println;
any_match(0) println;

-- Nested match
fn nested_match(x Int, y Int) String {
    match (x) {
        0: {
            match (y) {
                0: return "both zero";
                _: return "x zero";
            }
            "unreachable"
        }
        _: return "x nonzero";
    }
    "unreachable"
}
nested_match(0, 0) println;
nested_match(0, 1) println;
nested_match(1, 0) println;
