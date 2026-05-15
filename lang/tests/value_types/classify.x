-- Phase 0 type classification: verify each Repr category is detected.

-- Atom types
typeRepr(0);
typeRepr(0.0);
typeRepr(true);
typeRepr('sym);

-- Pointer types (Phase 0: Fraction/Complex still Pointer; Phase 4 reclassifies)
typeRepr("x");
typeRepr([1, 2, 3]);
typeRepr(1/2);
typeRepr(1.0+2.0i);

-- Empty enum -> DiscriminantEnum
enum Color {
    red,
    green,
    blue
}
let c = Color.red;
typeRepr(c);

-- Single-case empty enum
enum One { only }
let o = One.only;
typeRepr(o);

-- Nullable-pointer enum: built-in Option<String>
let opt = Option<String>.some("x");
typeRepr(opt);

-- Tuple struct over an atom (Phase 1 keeps these boxed -> Inline classification;
-- Phase 4 will reclassify storage to actually inline)
struct NodeId(Int);
let n = NodeId(7);
typeRepr(n);

struct Wgt(Float);
let w = Wgt(0.5);
typeRepr(w);

-- Tuple struct over a Pointer-Repr inner -> UnwrappedTupleStruct (Phase 1 acts on this)
struct Handle(String);
let h = Handle("res");
typeRepr(h);

struct Bag([Int]);
let b = Bag([1, 2]);
typeRepr(b);

-- Multi-field struct of atoms -> Inline
struct Pair { x Int, y Int }
let p = Pair { x: 1, y: 2 };
typeRepr(p);

-- 4-field struct (at the field-count threshold)
struct Quad { a Int, b Int, c Int, d Int }
let q = Quad { a: 1, b: 2, c: 3, d: 4 };
typeRepr(q);

-- 5-field struct -> Heap (over field-count limit)
struct Five { a Int, b Int, c Int, d Int, e Int }
let fv = Five { a: 1, b: 2, c: 3, d: 4, e: 5 };
typeRepr(fv);

-- Struct with a String field: String is Pointer (1 word, value type),
-- so Pair { Int, String } is still Inline 2 words.
struct WithStr { tag Int, name String }
let ws = WithStr { tag: 1, name: "hi" };
typeRepr(ws);

-- Tuples
let t2 = (1, 2);
typeRepr(t2);

let t4 = (1, 2, 3, 4);
typeRepr(t4);

let t5 = (1, 2, 3, 4, 5);
typeRepr(t5);

-- Enum with mixed payloads
enum Shape {
    circle Float,
    rect (Float, Float),
    point
}
typeRepr(Shape.point);

-- Phase 4g.1 eligibility: nested inline composition. Outer { p Pair; q Int }
-- has 2 fields; Pair contributes 2 inline words + Int contributes 1 = 3 words,
-- 3 fields after flattening -- still under the 4-word/4-field limits.
struct Outer { p Pair, q Int }
let ot = Outer { p: Pair { x: 1, y: 2 }, q: 3 };
typeRepr(ot);

-- Composite that exceeds the 4-word footprint: tuple of 5 Ints already
-- shown above. Here, a struct of (Pair, Pair, Int) would be 5 words which
-- is over the limit and must NOT be marked inline.
struct TooBig { a Pair, b Pair, c Int }
let tb = TooBig { a: Pair { x: 1, y: 2 }, b: Pair { x: 3, y: 4 }, c: 5 };
typeRepr(tb);

-- Enum with all-Complex payloads (Complex is Inline 2 words). Enum with
-- 2 such cases -> 1 disc + 2 payload words = 3 words. Under threshold.
enum Mix {
    asComplex Complex,
    asInt Int
}
typeRepr(Mix.asInt(7));
