-- QA: BUG - Template function with function type parameter doesn't resolve

-- This template takes a function type (T) T
fn apply_twice<T>(f (T) T, x T) T = f(f(x));

-- This now works:
apply_twice(fn(x Int) Int { x * 2 }, 3) println;

-- Concrete version also still works
fn apply_twice_int(f (Int) Int, x Int) Int = f(f(x));
apply_twice_int(fn(x Int) Int { x * 2 }, 3) println;

-- This non-template version works fine
fn apply_int(f (Int) Int, x Int) Int = f(x);
apply_int(fn(x Int) Int { x * 2 }, 3) println;

-- Templates with non-function args work fine
fn double_apply<T>(x T) = (x, x);
double_apply(42) println;
double_apply("hi") println;
