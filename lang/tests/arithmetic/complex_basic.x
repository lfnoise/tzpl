-- Basic complex arithmetic

-- Pure imaginary
1i println;
2i println;
println(1i + 2i);

-- i * i = -1
println(1i * 1i);

-- Complex construction
let z1 = Complex(3.0, 4.0);
let z2 = Complex(1.0, -1.0);
z1 println;
z2 println;

-- Addition
println(z1 + z2);

-- Subtraction
println(z1 - z2);

-- Multiplication: (3+4i)(1-i) = 3-3i+4i-4i^2 = 3+i+4 = 7+i
println(z1 * z2);

-- Division
println(z1 / z2);

-- Negation
println(-z1);
println(-z2);

-- Complex + real
let z3 = Complex(3.0, 4.0);
println(z3 + 2.0);
println(z3 * 3.0);

-- Equality
let a = Complex(1.0, 2.0);
let b = Complex(1.0, 2.0);
let c = Complex(1.0, 3.0);
println(a == b);
println(a == c);
println(a != c);
