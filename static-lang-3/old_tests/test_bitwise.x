-- Built-in bitwise operations on integers
println(255 & 15);
println(240 | 15);
println(255 ^ 15);
println(~0);
println(1 << 10);
println(1024 >> 5);

-- Bitwise operator overloading on a custom type
struct Mask { bits Int }

fn &(a Mask, b Mask) Mask = Mask { bits: a.bits & b.bits };
fn |(a Mask, b Mask) Mask = Mask { bits: a.bits | b.bits };
fn ^(a Mask, b Mask) Mask = Mask { bits: a.bits ^ b.bits };
fn <<(a Mask, n Int) Mask = Mask { bits: a.bits << n };
fn >>(a Mask, n Int) Mask = Mask { bits: a.bits >> n };

let m1 = Mask { bits: 255 };
let m2 = Mask { bits: 15 };
println(m1 & m2);
println(m1 | m2);
println(m1 ^ m2);
println(m1 << 4);
println(m1 >> 4);
