-- Default function arguments

-- Basic default: single default parameter
fn greet(name String, greeting String = "Hello") String = greeting $ " " $ name;
greet("World", "Hi") println;
greet("World") println;

-- Multiple defaults
fn range_info(start Int = 0, stop Int = 10, step Int = 1) String {
    "%^ to %^ by %^" fmt(start, stop, step)
}
range_info(1, 5, 2) println;
range_info(1, 5) println;
range_info(1) println;
range_info() println;

-- Default referencing preceding parameter
fn make_range(lo Int, hi Int = lo + 10) String = "%^ to %^" fmt(lo, hi);
make_range(5, 20) println;
make_range(5) println;

-- Expression-body with defaults
fn add(x Int, y Int = 1) Int = x + y;
add(10, 5) println;
add(10) println;

-- Default with various types
fn int_default(x Int = 42) Int = x;
int_default(7) println;
int_default() println;

fn float_default(x Float = 3.14) Float = x;
float_default(1.0) println;
float_default() println;

fn string_default(x String = "hello") String = x;
string_default("world") println;
string_default() println;

fn bool_default(x Bool = true) Bool = x;
bool_default(false) println;
bool_default() println;

-- Defaults with computed expressions
fn power(base Int, exp Int = 2) Int {
    var result = 1;
    var i = 0;
    while (i < exp) {
        result = result * base;
        i = i + 1;
    }
    result
}
power(3, 4) println;
power(5) println;

-- Default with preceding param reference in expression body
fn offset(x Int, dx Int = x) Int = x + dx;
offset(5, 3) println;
offset(5) println;
