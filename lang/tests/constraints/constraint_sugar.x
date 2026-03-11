-- Constraint-as-param-type sugar tests
-- fn foo(a Constraint) desugars to fn foo<__T0: Constraint>(a __T0)

constraint Numeric = Int | Float;

-- Basic: constraint name as param type
fn show_num(x Numeric) String = toString(x);

show_num(42) println;
show_num(3.14) println;

-- Multiple params with same constraint get independent type params
-- This allows different types for each param (unlike fn foo<T: C>(a T, b T))
fn show_both(a Numeric, b Numeric) String = toString(a) $ " " $ toString(b);

show_both(1, 2) println;
show_both(1.5, 2.5) println;
show_both(1, 2.5) println;

-- Structural constraint sugar
constraint Addable<T> = requires {
    +(T, T) T
};

fn double_sugar(x Addable) String = toString(x + x);

double_sugar(10) println;
double_sugar(3.14) println;

-- Mix explicit generics with constraint sugar
fn mixed<T: Numeric>(a T, b Addable) String = toString(a);

mixed(5, 10) println;
mixed(2.0, 3) println;

-- Constraint sugar alongside concrete types
fn describe_sugar(x Numeric, label String) String = label;

describe_sugar(42, "hello") println;

-- Composition constraint sugar
constraint NumericAddable<T> = Numeric & Addable<T>;

fn triple_sugar(x NumericAddable) String = toString(x + x + x);

triple_sugar(5) println;
triple_sugar(1.0) println;
