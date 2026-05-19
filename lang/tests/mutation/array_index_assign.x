-- a[i] = x for arrays, across element-type backends and wrapped indices.

-- Int backend
var ai = [1, 2, 3, 4, 5];
ai[0] = 100;
ai[2] = 200;
ai println;

-- Wrapping: negative index wraps to size + i
ai[-1] = 999;
ai println;

-- Wrapping: index >= length wraps via cyclicIndex
ai[7] = 42;    -- 7 mod 5 == 2
ai println;

-- Float backend
var af = [1.0, 2.0, 3.0];
af[1] = 20.5;
af println;

-- String (Obj) backend
var ao = ["a", "b", "c"];
ao[0] = "AA";
ao[-1] = "CC";
ao println;

-- Promotion: Int RHS to Float element
var ap = [0.0, 0.0, 0.0];
ap[0] = 7;
ap println;
