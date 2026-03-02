-- Complex number tests

-- Pure imaginary
let i1 = 1i;
let i2 = 2i;
let i_sum = i1 + i2;
println(i_sum);

-- i * i = -1
let i_sq = i1 * i1;
println(i_sq);

-- Complex construction
let z1 = Complex(3.0, 4.0);
let z2 = Complex(1.0, -1.0);
println(z1);
println(z2);

-- Arithmetic
println(z1 + z2);
println(z1 - z2);
println(z1 * z2);
println(z1 / z2);

-- Complex conjugate via construction
-- conj(a+bi) = a-bi = Complex(a, -b)
-- Can't extract real/imag yet, but we can test with known values
let z3 = Complex(5.0, 0.0);
let z4 = Complex(0.0, 3.0);
println(z3 + z4);
println(z3 * z4);

-- Negation
println(-z1);
println(-z2);

-- Complex * real (via promotion)
-- 2.0 gets promoted to Complex(2.0, 0.0)
-- Then (3+4i) * (2+0i) = 6+8i
let scaled = z1 * Complex(2.0, 0.0);
println(scaled);

-- Powers of i: i^1=i, i^2=-1, i^3=-i, i^4=1
let i_1 = 1i;
let i_2 = i_1 * i_1;
let i_3 = i_2 * i_1;
let i_4 = i_3 * i_1;
println(i_1);
println(i_2);
println(i_3);
println(i_4);

-- Complex number equality
let a = Complex(1.0, 2.0);
let b = Complex(1.0, 2.0);
let c = Complex(1.0, 3.0);
if (a == b) {
    println("a == b: true");
} else {
    println("a == b: false");
}
if (a == c) {
    println("a == c: true");
} else {
    println("a == c: false");
}
if (a != c) {
    println("a != c: true");
} else {
    println("a != c: false");
}
