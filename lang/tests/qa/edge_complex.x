-- QA: Complex number operations

-- Construction
let z = 3.0 + 4i;
z println;

-- Decomposition
real(z) println;     -- 3.0
imag(z) println;     -- 4.0
abs(z) println;      -- 5.0
conj(z) println;     -- 3-4i

-- Complex arithmetic
let a = 1.0 + 2i;
let b = 3.0 + 4i;
println(a + b);      -- 4+6i
println(a - b);      -- -2-2i
println(a * b);      -- -5+10i

-- Pure imaginary
let pi2 = 0.0 + 1i;
println(pi2 * pi2);  -- -1+0i

-- Complex equality
println((1.0 + 2i) == (1.0 + 2i));  -- true
println((1.0 + 2i) != (3.0 + 4i));  -- true
println((1.0 + 2i) == (1.0 + 3i));  -- false

-- Zero complex
println(0.0 + 0i);   -- 0+0i

-- Negative imaginary
println(3.0 - 4i);

-- Complex with integer promotion
println(1 + 2i);
println(0 + 1i);

-- Complex math functions
println(sqrt(Complex(-1.0, 0.0)));  -- should be ~0+1i

-- Complex norm (|z|^2)
norm(z) println;     -- 25.0

-- arg (angle)
arg(1.0 + 0i) println;   -- 0.0
