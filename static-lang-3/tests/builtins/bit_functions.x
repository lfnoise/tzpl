-- Bitwise builtin operations

-- Basic bitwise
println(255 & 15);
println(240 | 15);
println(255 ^ 15);
println(~0);
println(~255);
println(1 << 10);
println(1024 >> 5);

-- Combined
println((255 & 15) | 240);
println((1 << 8) - 1);
println(65535 >> 8);

-- Bit counting / manipulation
println(0xFF popCount);
println(0x0F popCount);
println(1 clz);
println(8 hasSingleBit);
println(6 hasSingleBit);

-- Right shift
println(256 >> 1);
println(256 >> 2);
println(256 >> 4);
println(256 >> 8);
