-- Type parameter constraint tests

-- Type-set constraint
constraint Numeric = Int | Float;

-- Constrained function (inline syntax)
fn add_nums<T: Numeric>(a T, b T) T = a + b;

-- Should work with Int
add_nums(1, 2) println;

-- Should work with Float
add_nums(1.5, 2.5) println;

-- Structural constraint
constraint Addable<T> = requires {
    +(T, T) T
};

fn double<T: Addable>(x T) T = x + x;

double(10) println;
double(3.14) println;

-- Composition constraint
constraint NumericAddable<T> = Numeric & Addable<T>;

fn triple<T: NumericAddable>(x T) T = x + x + x;

triple(5) println;
triple(1.0) println;

-- Where clause syntax
fn multiply<T>(a T, b T) T where T: Numeric = a * b;

multiply(3, 4) println;
multiply(2.0, 3.0) println;

-- Constraint on struct template
struct Pair<T: Numeric> { first T, second T }

let p = Pair { first: 1, second: 2 };
p.first println;
p.second println;

let fp = Pair { first: 1.0, second: 2.0 };
fp.first println;
fp.second println;

-- Constraint on enum template
enum NumResult<T: Numeric> { ok T, err String }

let r = NumResult.ok(42);
match (r) {
    NumResult.ok(v): v println;
    NumResult.err(s): s println;
}

-- Constraint-based overload filtering:
-- Only the matching overload should be selected
fn describe<T: Numeric>(x T) String = "numeric";
fn describe(x String) String = "string";

describe(42) println;
describe("hello") println;

-- === Unified constraint composition: constraint | type in union ===

-- Constraint that mixes types and constraint refs with |
constraint Rational = Int | Fraction;
constraint Real = Rational | Float;

fn show_real<T: Real>(x T) String = toString(x);

show_real(42) println;
show_real(3.14) println;
show_real(1/3) println;

-- Multi-level composition: constraint | type
constraint Number = Real | Complex;

fn show_number<T: Number>(x T) String = toString(x);

show_number(7) println;
show_number(2.5) println;
show_number(1/2) println;
show_number(3+4i) println;

-- Recursive constraint with parameterized types
constraint NumericArray = [Number] | [NumericArray];

fn describe_na<T: NumericArray>(arr T) String = "numeric-array";

-- Array of Number types
describe_na([1, 2, 3]) println;
describe_na([1.0, 2.0]) println;

-- Nested: array of arrays of numbers
describe_na([[1, 2], [3, 4]]) println;

-- Triple-nested
describe_na([[[1, 2]], [[3]]]) println;
