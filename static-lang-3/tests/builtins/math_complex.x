-- Complex math builtins

-- Basic construction
let z1 = Complex(3.0, 4.0);
let z2 = Complex(1.0, -1.0);
z1 println;
z2 println;

-- Arithmetic
println(z1 + z2);
println(z1 - z2);
println(z1 * z2);
println(z1 / z2);
println(-z1);

-- real, imag
z1 real println;
z1 imag println;
z2 real println;
z2 imag println;

-- mag (magnitude)
Complex(3.0, 4.0) mag println;
Complex(0.0, 1.0) mag println;
Complex(1.0, 0.0) mag println;

-- phase
Complex(1.0, 0.0) phase println;
Complex(0.0, 1.0) phase println;

-- conj (conjugate)
z1 conj println;
z2 conj println;
