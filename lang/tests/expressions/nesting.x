-- Deeply nested expressions

-- Nested arithmetic
println(((1 + 2) * (3 + 4)) + ((5 - 6) * (7 - 8)));
println((2 + 3) * (4 + 5) * (6 + 7));

-- Nested function calls
fn add(a Int, b Int) Int = a + b;
fn mul(a Int, b Int) Int = a * b;
println(add(mul(2, 3), mul(4, 5)));
println(mul(add(1, 2), add(3, 4)));

-- Nested ternary
println(true ? (false ? 1 : 2) : 3);
println(false ? 1 : (true ? 2 : 3));

-- Array of arrays
let m = [[1, 2], [3, 4]];
println(m[0][0] + m[1][1]);

-- Nested struct
struct Inner { val Int }
struct Outer { inner Inner, name String }
let o = Outer { inner: Inner { val: 42 }, name: "test" };
println(o.inner.val);
println(o.name);
