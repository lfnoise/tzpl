-- Default arguments with inferred types (no explicit type annotation)

-- Inferred literal types: String
fn greet(name = "world") String = "Hello " $ name;
greet() println;
greet("Claude") println;

-- Inferred literal types: Int
fn double(x Int, n = 2) Int = x * n;
double(5) println;
double(5, 3) println;

-- Inferred literal types: Float
fn scale(x Float, factor = 1.0) Float = x * factor;
scale(3.0) println;
scale(3.0, 2.5) println;

-- Inferred literal types: Bool
fn maybe_negate(x Int, negate = false) Int {
    if (negate) { 0 - x } else { x }
}
maybe_negate(7) println;
maybe_negate(7, true) println;

-- Inferred literal types: Fraction
fn add_frac(x Int, f = 1/2) String = "%^ + %^" fmt(x, f);
add_frac(3) println;
add_frac(3, 3/4) println;

-- Mixed: typed and inferred defaults
fn config(x Int, verbose = false, label = "default") String {
    "%^ %^ %^" fmt(label, x, verbose)
}
config(1) println;
config(2, true) println;
config(3, true, "custom") println;

-- Inferred from preceding parameter reference
fn glunk(x Int, y = x) Int = x + y;
glunk(2) println;
glunk(3, 10) println;

-- Chained defaults referencing earlier params
fn chain(a Int, b = a + 1, c = b + 1) Int = a + b + c;
chain(1) println;
chain(1, 10) println;
chain(1, 10, 100) println;

-- Preceding param reference with expression
fn make_range(lo Int, hi = lo + 10) String = "%^ to %^" fmt(lo, hi);
make_range(5, 20) println;
make_range(5) println;

-- Preceding param reference: multiply
fn area(width Int, height = width) Int = width * height;
area(4, 6) println;
area(5) println;

-- Template function with inferred default
fn show<T>(x T, label = "value") String = label $ ": " $ toString(x);
show(42) println;
show(42, "answer") println;
show(3.14) println;
show(3.14, "pi") println;

-- Template with inferred default (Int literal)
fn nth<T>(xs [T], n = 0) T = xs[n];
nth([10,20,30]) println;
nth([10,20,30], 2) println;

-- All-inferred single param
fn get_greeting(greeting = "Hi there") String = greeting;
get_greeting() println;
get_greeting("Howdy") println;

-- Inferred default with return type inferred too
fn add_default(x Int, y = 1) = x + y;
add_default(10) println;
add_default(10, 5) println;

-- Preceding param default on a template
fn pair_or_double<T>(x T, y = x) = (x, y);
pair_or_double(7) println;
pair_or_double(7, 99) println;
pair_or_double("hi") println;
pair_or_double("hi", "lo") println;
