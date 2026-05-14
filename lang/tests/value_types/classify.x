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
