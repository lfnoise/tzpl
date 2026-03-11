-- Hexadecimal integer literals

-- Basic hex values
println(0x0);
println(0x1);
println(0xA);
println(0xF);
println(0xFF);
println(0x100);

-- Mixed case hex digits
println(0xab);
println(0xAB);
println(0xaBcDeF);

-- Arithmetic with hex literals
println(0x10 + 0x20);
println(0xFF - 0x0F);
println(0x10 * 0x10);
println(0x100 // 0x10);
println(0xFF % 0x10);

-- Hex in expressions
let x = 0xFF;
println(x);
println(x + 1);

-- Hex with 0X prefix
println(0X1A);
println(0XFF);

-- Larger values
println(0xDEADBEEF);
println(0x1234ABCdef);

-- Comparison with decimal equivalents
println(0x0A == 10);
println(0x10 == 16);
println(0xFF == 255);
println(0x100 == 256);

-- Hex in function arguments
fn double(n Int) Int = n * 2;
println(double(0x20));
