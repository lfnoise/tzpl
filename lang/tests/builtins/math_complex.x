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

-- abs (magnitude)
Complex(3.0, 4.0) abs println;
Complex(0.0, 1.0) abs println;
Complex(1.0, 0.0) abs println;

-- arg (phase)
Complex(1.0, 0.0) arg println;
Complex(0.0, 1.0) arg println;

-- conj (conjugate)
z1 conj println;
z2 conj println;
