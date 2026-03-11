-- Constraint violation: String is not Numeric
constraint Numeric = Int | Float;
fn add_nums<T: Numeric>(a T, b T) T = a + b;
add_nums("hello", "world");
