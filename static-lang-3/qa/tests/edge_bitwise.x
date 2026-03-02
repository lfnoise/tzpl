-- QA: Edge cases in bitwise operations

-- Basic bitwise
println(0xFF & 0x0F);     -- 15
println(0xFF | 0x100);    -- 511
println(0xFF ^ 0xFF);     -- 0
println(~0);              -- -1 (all bits set)
println(~1);              -- -2

-- Shifts
println(1 << 0);          -- 1
println(1 << 1);          -- 2
println(1 << 10);         -- 1024
println(1 << 32);         -- 4294967296
println(1024 >> 10);      -- 1
println(1024 >> 0);       -- 1024

-- Logical right shift
println(-1 >>> 63);       -- 1
println(-1 >>> 0);        -- -1

-- Bit counting functions
println(clz(0));           -- 64
println(clz(1));           -- 63
println(clz(0xFF));        -- 56
println(ctz(0));           -- 64
println(ctz(1));           -- 0
println(ctz(8));           -- 3
println(popCount(0));      -- 0
println(popCount(0xFF));   -- 8
println(popCount(0xFFFF)); -- 16
println(popCount(-1));     -- 64

-- Leading/trailing ones
println(clo(0));           -- 0
println(clo(-1));          -- 64
println(cto(0));           -- 0
println(cto(1));           -- 1
println(cto(7));           -- 3

-- Rotation
println(rotl(1, 1));       -- 2
println(rotl(1, 63));      -- should be MIN_INT
println(rotr(2, 1));       -- 1

-- Power of 2 utilities
println(bitCeil(1));       -- 1
println(bitCeil(3));       -- 4
println(bitCeil(4));       -- 4
println(bitCeil(5));       -- 8
println(bitFloor(1));      -- 1
println(bitFloor(3));      -- 2
println(bitFloor(4));      -- 4
println(bitFloor(5));      -- 4
println(bitWidth(0));      -- 0
println(bitWidth(1));      -- 1
println(bitWidth(7));      -- 3
println(bitWidth(8));      -- 4
println(hasSingleBit(0));  -- false
println(hasSingleBit(1));  -- true
println(hasSingleBit(2));  -- true
println(hasSingleBit(3));  -- false
println(hasSingleBit(4));  -- true
println(hasSingleBit(6));  -- false

-- Hexadecimal integers
println(0xFF);
println(0x100);
println(0xDeadBeef);
