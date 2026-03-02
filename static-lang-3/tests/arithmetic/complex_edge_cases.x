-- Complex edge cases

-- Powers of i: i^1=i, i^2=-1, i^3=-i, i^4=1
let i_1 = 1i;
let i_2 = i_1 * i_1;
let i_3 = i_2 * i_1;
let i_4 = i_3 * i_1;
i_1 println;
i_2 println;
i_3 println;
i_4 println;

-- Complex with zero imaginary
Complex(5.0, 0.0) println;

-- Complex with zero real
Complex(0.0, 3.0) println;

-- Complex scaled by complex
let z = Complex(3.0, 4.0);
let scaled = z * Complex(2.0, 0.0);
scaled println;

-- Int + Complex (promotion)
let zint = 10 + 3i;
zint println;
