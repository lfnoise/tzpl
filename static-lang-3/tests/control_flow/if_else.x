-- If/else statements

-- Basic if
if (true) { "yes" println; }

-- Basic if/else
if (true) { "true branch" println; } else { "false branch" println; }
if (false) { "true branch" println; } else { "false branch" println; }

-- If with comparison
let x = 10;
if (x > 5) { "big" println; } else { "small" println; }
if (x < 5) { "big" println; } else { "small" println; }

-- Nested if/else
fn classify(n Int) String {
    if (n < 0) {
        return "negative";
    } else if (n == 0) {
        return "zero";
    } else if (n < 10) {
        return "small";
    } else if (n < 100) {
        return "medium";
    } else {
        return "large";
    }
}

-5 classify println;
0 classify println;
7 classify println;
42 classify println;
999 classify println;

-- If with boolean variable
let flag = true;
if (flag) { "flag is true" println; }

-- If with logical operators
if (true && true) { "both true" println; }
if (true && false) { "should not print" println; } else { "and failed" println; }
if (false || true) { "or succeeded" println; }
