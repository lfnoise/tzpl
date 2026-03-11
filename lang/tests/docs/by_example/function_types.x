-- Function Type Annotations from Language X by Example

-- Function type annotation on a variable
let add (Int, Int) Int = fn(a Int, b Int) Int { a + b };
add(3, 4) println;

-- Single-argument function type
let double (Int) Int = fn(x Int) Int { x * 2 };

-- Zero-argument function type
let getVal () Int = fn() Int { 42 };

-- Function type as parameter
fn apply(f (Int) Int, x Int) Int = f(x);
apply(double, 5) println;

-- Function returning a function type
fn make_adder(n Int) (Int) Int {
    fn(x Int) Int { x + n }
}
let add5 = make_adder(5);
10 add5 println;

-- Compose: function taking two functions, returning a function
fn compose(f (Int) Int, g (Int) Int) (Int) Int {
    fn(x Int) Int { f(g(x)) }
}

let inc = fn(x Int) Int { x + 1 };
let dbl = fn(x Int) Int { x * 2 };
let inc_then_dbl = compose(dbl, inc);
3 inc_then_dbl println;
