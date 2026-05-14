-- Phase 4f: Complex and Fraction as 2-word inline value types.

-- Construction / literals
let z = 3.0 + 4i;
let f = 1/2;
println(z);          -- 3+4i
println(f);          -- 1/2

-- Math (no allocations during arithmetic)
println(z + (1.0 + 2i));   -- 4+6i
println(z - (1.0 + 2i));   -- 2+2i
println(z * (1.0 + 2i));   -- -5+10i
println(-z);               -- -3-4i
println(z == (3.0 + 4i));  -- true
println(z != (1.0 + 0i));  -- true

println(f + 1/3);          -- 5/6
println(f - 1/3);          -- 1/6
println(f * 2/3);          -- 1/3
println(f / 2);            -- 1/4
println(f == 1/2);         -- true
println(f < 3/4);          -- true

-- Conversions
println(2 + 3i);              -- 2+3i (Int promoted to Complex)
println(1/4 + 1);             -- 5/4 (Int promoted to Fraction)
let g = (1/2) toFloat;
println(g);                    -- 0.5

-- Math builtins
println(real(z));   -- 3.0
println(imag(z));   -- 4.0
println(abs(z));    -- 5.0
println(conj(z));   -- 3-4i

-- Re-assignment overwrites both words
var w = 1.0 + 2i;
w = 5.0 + 6i;
println(w);          -- 5+6i

var q = 1/2;
q = 3/4;
println(q);          -- 3/4

-- Globals (boxed at storage boundary)
let cg = 7.0 + 8i;
let fg = 9/10;
println(cg);
println(fg);

-- Tuple containing inline values (each field is boxed in the heap Tuple)
let tup = (1.0 + 2i, 3/4);
println(tup);                  -- (1+2i, 3/4)
println(tup.0);                -- 1+2i
println(tup.1);                -- 3/4

-- Destructuring inline values from a tuple
let (a, b) = (1/2, 3/4);
println(a);                    -- 1/2
println(b);                    -- 3/4

-- Array of inline values (boxed in ObjArray, unboxed when iterated)
let arr = [1/2, 1/3, 1/4];
println(arr);                  -- [1/2, 1/3, 1/4]
println(arr[0]);               -- 1/2
var total = 0/1;
for (x : arr) {
    total = total + x;
}
println(total);                -- 13/12

-- Map with inline value and inline key
let m = ["k": 1.0 + 2i];
println(m["k"]);               -- Option<Complex>.some(1+2i)
let m2 = [(1.0 + 2i): "first"];
println(m2[1.0 + 2i]);         -- Option<String>.some(first)

-- Struct with inline field
struct Pt { x Complex, y Float }
let p = Pt { x: 1.0 + 2i, y: 3.0 };
println(p.x);                  -- 1+2i
println(p.y);                  -- 3.0

-- User function taking and returning Complex
fn double_z(z Complex) Complex = z * 2;
println(double_z(1.0 + 2i));   -- 2+4i

-- User function taking and returning Fraction
fn invert(f Fraction) Fraction = 1/f;
println(invert(2/3));          -- 3/2

-- Lambda
let mul3 = fn(z Complex) Complex { z * 3 };
println(mul3(1.0 + 1i));       -- 3+3i

-- Range of Fraction (inline range)
for (i : (1/2..5/2)) {
    println(i);
}
