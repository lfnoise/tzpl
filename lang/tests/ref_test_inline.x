-- Phase 4g.5: Refs to inline composites (Tuples, Structs, Inline Enums)
-- Verifies that Ref[InlineComposite] stores its payload inline in InlineRef::v[]
-- and that mutation through any alias is visible through every other alias.

struct Point { x Int, y Int }
struct Named { id Int, name String }

enum Shape {
    circle Float,
    rect (Int, Int),
    nothing
}

-- ref() builtin with tuple element type
let r1 = ref((1, 2, 3));
deref(r1) println;
setref((10, 20, 30), r1);
deref(r1) println;
((100, 200, 300)) setref(r1);
deref(r1) println;

-- ref() builtin with struct element type
let r2 = ref(Point{1, 2});
deref(r2) println;
setref(Point{10, 20}, r2);
deref(r2) println;

-- & and * operator forms (parallel to ref/deref builtins)
let p = Point{5, 7};
let r3 = &p;
*r3 println;
r3 <- Point{50, 70};
*r3 println;

-- Mix: builtin ref() + operator *, then operator <- + builtin deref
let r4 = ref(Point{1, 1});
*r4 println;
r4 <- Point{2, 2};
deref(r4) println;

-- Aliasing: mutating through one alias is visible through the other
let r5 = ref((0, 0));
let alias = r5;
setref((42, 99), r5);
deref(alias) println;
alias <- (7, 8);
*r5 println;

-- ARC: String payload field rewritten many times (each setref releases old, retains new)
let r6 = ref(Named{0, "a"});
setref(Named{1, "b"}, r6);
setref(Named{2, "c"}, r6);
setref(Named{3, "d"}, r6);
deref(r6) println;

-- Inline enum (multiple cases with different payload shapes)
let r7 = ref(Shape.circle(1.5));
deref(r7) println;
setref(Shape.rect((3, 4)), r7);
deref(r7) println;
setref(Shape.nothing, r7);
deref(r7) println;
r7 <- Shape.circle(2.71);
*r7 println;
