-- Bitwise operators

-- AND
println(255 & 15);
println(0 & 255);
println(255 & 255);
println(12 & 10);

-- OR
println(240 | 15);
println(0 | 255);
println(12 | 10);

-- XOR
println(255 ^ 15);
println(0 ^ 255);
println(12 ^ 10);
println(42 ^ 42);

-- NOT (bitwise complement)
println(~0);
println(~1);
println(~(-1));
println(~255);

-- Left shift
println(1 << 0);
println(1 << 1);
println(1 << 10);
println(3 << 4);
println(255 << 8);

-- Right shift
println(1024 >> 5);
println(256 >> 4);
println(255 >> 1);
println(1 >> 1);
println(48 >> 4);

-- Unsigned right shift
println(1024 >>> 5);
println(256 >>> 4);
println((-1) >>> 1);
println((-1) >>> 32);
println((-256) >>> 4);

-- Combined
println((1 << 8) | 255);
println((65280 & 65280) >> 8);
