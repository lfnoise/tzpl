-- Phase 4g.27: every builtin uses the native multi-word ABI at its call
-- boundary. Args and returns that are Inline composites (Complex, Fraction,
-- Inline Tuple/Struct/Enum) travel as sizeWords_-wide register windows.
-- These tests exercise the migrated builtins so that:
--
--   * fold / scan / fold1 / scan1 / iter no longer use the box-then-unbox
--     boundary helpers (readBoundaryArg / writeBoundaryResult).
--   * repeat dispatches on arrayBackendFor so Inline composites land in an
--     InlineArray, not an ObjArray (the old _obj implementation had
--     length=0 on consumers that asked for InlineArray's stride).
--   * cons reads its element type from the resolved Primitive instead of
--     calling .o->type_ on a multi-word Complex/Fraction first word.
--   * pick writes its result natively into dst.. (multi-word).
--   * any / toAnyArray box Inline composite args into a single Obj* via
--     boxPayload so AnyObj's 1-Word value_ slot can hold them.

struct Pt { x Int; y Int }
struct Box { p Pt; n Int }

-- --- fold / scan / fold1 / scan1 with Inline composite accumulator ---
fold([1.0+2.0i, 3.0+4.0i], 0.0+0.0i, fn(a Complex, x Complex) Complex { a + x }) println;
fold([1/2, 3/4], 0/1, fn(a Fraction, x Fraction) Fraction { a + x }) println;
fold([(1, 2), (3, 4)], (0, 0), fn(a (Int, Int), x (Int, Int)) (Int, Int) { (a.0+x.0, a.1+x.1) }) println;
fold([Pt{x:1,y:2}, Pt{x:3,y:4}], Pt{x:0,y:0}, fn(a Pt, x Pt) Pt { Pt{x:a.x+x.x, y:a.y+x.y} }) println;

scan([(1,2), (3,4)], (0,0), fn(a (Int,Int), x (Int,Int)) (Int,Int) { (a.0+x.0, a.1+x.1) }) println;
fold1([(1,2), (3,4), (5,6)], fn(a (Int,Int), x (Int,Int)) (Int,Int) { (a.0+x.0, a.1+x.1) }) println;
scan1([Pt{x:1,y:2}, Pt{x:3,y:4}], fn(a Pt, x Pt) Pt { Pt{x:a.x+x.x, y:a.y+x.y} }) println;

-- List versions
fold(List(1.0+2.0i, 3.0+4.0i), 0.0+0.0i, fn(a Complex, x Complex) Complex { a + x }) println;
fold(List((1,2), (3,4)), (0,0), fn(a (Int,Int), x (Int,Int)) (Int,Int) { (a.0+x.0, a.1+x.1) }) println;
fold1(List((1,2), (3,4), (5,6)), fn(a (Int,Int), x (Int,Int)) (Int,Int) { (a.0+x.0, a.1+x.1) }) println;
scan(List(1.0+2.0i, 3.0+4.0i), 0.0+0.0i, fn(a Complex, x Complex) Complex { a + x }) println;

-- --- iter with Inline composite seed/return ---
iter((0, 1), fn(p (Int, Int)) (Int, Int) { (p.1, p.0 + p.1) }) take(8) println;
iter(Pt{x:1, y:1}, fn(p Pt) Pt { Pt{x: p.y, y: p.x + p.y} }) take(5) println;
iter(1.0+1.0i, fn(c Complex) Complex { c * (2.0+0.0i) }) take(4) println;
iter(1/2, fn(f Fraction) Fraction { f + 1/2 }) take(4) println;

-- --- repeat with all backends ---
repeat(1.0+2.0i, 3) println;          -- PodArray<x64>
repeat(1/2, 3) println;               -- PodArray<r64>
repeat((1, 2), 3) println;            -- InlineArray
repeat(Pt{x:1, y:2}, 3) println;      -- InlineArray
repeat(Box{p:Pt{x:1,y:2}, n:3}, 2) println;  -- InlineArray (4 words)
repeat(1, 3) println;                 -- PodArray<i64>
repeat("hi", 3) println;              -- ObjArray
repeat(1.0+2.0i, 3) length println;
repeat((1, 2), 3) length println;
repeat(Pt{x:1, y:2}, 3) length println;
repeat((1, 2), 3)[0] println;
repeat(Pt{x:1, y:2}, 3)[2] println;

-- --- cons with Inline composite elem ---
cons(1.0+2.0i, List(3.0+4.0i)) println;
cons(1/2, List(3/4)) println;
cons((1, 2), List((3, 4))) println;
cons(Pt{x:1, y:2}, List(Pt{x:3, y:4})) println;

-- --- pick returning Inline composite ---
pick([1.0+2.0i]) println;             -- always picks the lone element
pick([1/2]) println;
pick([(1, 2)]) println;
pick([Pt{x:1, y:2}]) println;

-- --- any / toAnyArray with Inline composite ---
any(1.0+2.0i) println;
any(1/2) println;
any((1, 2)) println;
any(Pt{x:1, y:2}) println;
toAnyArray((1.0+2.0i, Pt{x:5, y:6}, "hi")) println;
toAnyArray((1/2, 1, 2.0)) println;
