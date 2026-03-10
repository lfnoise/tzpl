-- QA: Edge cases in pattern matching

-- Wildcard in different positions
fn test_wildcard(x Int) String {
    match (x) {
        _: return "anything";
    }
    "unreachable"
}
test_wildcard(42) println;
test_wildcard(0) println;
test_wildcard(-1) println;

-- Match on boolean
fn bool_match(b Bool) String {
    match (b) {
        true: return "yes";
        false: return "no";
    }
    "unreachable"
}
bool_match(true) println;
bool_match(false) println;

-- Match on string literals
fn string_match(s String) String {
    match (s) {
        "hello": return "greeting";
        "bye": return "farewell";
        _: return "unknown";
    }
    "unreachable"
}
string_match("hello") println;
string_match("bye") println;
string_match("other") println;

-- Nested tuple destructuring
let nested = ((1, 2), (3, 4));
match (nested) {
    ((a, b), (c, d)): {
        println(a + b + c + d);
    }
}

-- List pattern matching - empty list
fn list_len(lst List<Int>) Int {
    match (lst) {
        nil: return 0;
        h :: t: return 1 + list_len(t);
    }
    0
}
list_len(List(1, 2, 3)) println;
let empty List<Int> = nil;
list_len(empty) println;

-- Enum pattern matching with multiple cases
enum Expr {
    num Int,
    add (Int, Int),
    neg Int
}

fn eval(e Expr) Int {
    match (e) {
        Expr.num(n): return n;
        Expr.add(pair): return pair.0 + pair.1;
        Expr.neg(n): return -n;
    }
    0
}
eval(Expr.num(42)) println;
eval(Expr.add((3, 4))) println;
eval(Expr.neg(10)) println;

-- Guard with complex expression
fn classify(x Int, y Int) String {
    match (x) {
        n if (n > 0 && y > 0): return "both positive";
        n if (n < 0 && y < 0): return "both negative";
        0: return "x is zero";
        _: return "mixed";
    }
    "unreachable"
}
classify(1, 1) println;
classify(-1, -1) println;
classify(0, 5) println;
classify(1, -1) println;

-- Struct destructuring in match
struct Point { x Int, y Int }

fn describe_point(p Point) String {
    match (p) {
        Point { x: 0, y: 0 }: return "origin";
        Point { x: 0, y: _ }: return "on y-axis";
        Point { x: _, y: 0 }: return "on x-axis";
        _: return "general";
    }
    "unreachable"
}
describe_point(Point{0, 0}) println;
describe_point(Point{0, 5}) println;
describe_point(Point{5, 0}) println;
describe_point(Point{3, 4}) println;

-- Match as expression (return value)
fn sign(x Int) Int {
    match (x) {
        n if (n > 0): return 1;
        n if (n < 0): return -1;
        0: return 0;
        _: return 0;
    }
    0
}
sign(42) println;
sign(-7) println;
sign(0) println;

-- Tuple destructuring in let
let (a, b, c) = (10, 20, 30);
a println;
b println;
c println;

-- Array destructuring
let [x, y, z] = [100, 200, 300];
x println;
y println;
z println;

-- Multiple match arms with same type
fn day_type(day String) String {
    match (day) {
        "Mon": return "weekday";
        "Tue": return "weekday";
        "Wed": return "weekday";
        "Thu": return "weekday";
        "Fri": return "weekday";
        "Sat": return "weekend";
        "Sun": return "weekend";
        _: return "unknown";
    }
    "unreachable"
}
day_type("Mon") println;
day_type("Sat") println;
day_type("xyz") println;
