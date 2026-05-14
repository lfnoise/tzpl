-- Phase 1: tuple-struct unwrap for Pointer-Repr inners.
-- Construction, field access, pattern matching, printing should all behave
-- identically to before, but no Struct allocation happens.

-- Single-field tuple struct over String (Pointer-Repr)
struct Handle(String);

let h = Handle("alpha");
println(h);             -- Handle(alpha)
println(h.0);           -- alpha (the inner)

-- Pattern matching: let destructuring
let Handle(name) = h;
println(name);          -- alpha

-- Pattern matching: match arm
fn describe(x Handle) String {
    match (x) {
        Handle(s): s;
    }
}
println(describe(h));   -- alpha
println(describe(Handle("beta")));  -- beta

-- Function parameter / return
fn wrap(s String) Handle = Handle(s);
let w = wrap("gamma");
println(w);             -- Handle(gamma)

-- Generic function over T = Handle
fn id<T>(x T) T = x;
let h2 = id(h);
println(h2);            -- Handle(alpha)
println(h2.0);          -- alpha

-- Equality (same inner reference -> equal handles)
let a = Handle("x");
let b = Handle("x");
println(a.0 == b.0);    -- true

-- Storage in arrays (ObjArray of String*, since UnwrappedTupleStruct(String))
let arr = [Handle("one"), Handle("two"), Handle("three")];
println(arr);                   -- [Handle(one), Handle(two), Handle(three)]
println(arr[0]);                -- Handle(one)
println(arr[1].0);              -- two

-- Tuple struct over an Array elem type (Pointer-Repr)
struct Bag([Int]);
let bg = Bag([10, 20, 30]);
println(bg);                    -- Bag([10, 20, 30])
println(bg.0);                  -- [10, 20, 30]
let Bag(xs) = bg;
println(xs);                    -- [10, 20, 30]
