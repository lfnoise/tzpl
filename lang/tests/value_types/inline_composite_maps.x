-- Phase 4g.23: Map/Set/Ref/zip/enumerate with inline composite K/V/elem.
-- These were broken in multiple ways:
--
--   * pairs() always built an ObjArray. For tuples that are Inline-repr
--     (e.g. (Int, Complex)), the consumer routed `length`/`@`/index through
--     the Inline backend (arrayBackendFor returns Inline for Inline tuples),
--     static_cast'd to InlineArray, and read garbage. `length` returned 0,
--     `@` produced nothing, indexing crashed.
--
--   * zip([Int], [Complex/Fraction]) on the Inline-tuple branch dropped the
--     Complex/Fraction unbox path, leaving 2-word fields half-written.
--     Result printed as `(1, 2.5e-314+0i)` etc. (random heap-pointer bits
--     in the second word). Same for enumerate.
--
--   * zip(List, List) / enumerate(List) with Complex/Fraction elements
--     segfaulted because writeListHeadField excluded Complex/Fraction and
--     wrote only 1 word into a 2-word native list-head slot.
--
--   * ref(1.0+2.0i) and ref(1/2) segfaulted: resolve_ref's "inlineComposite"
--     gate excluded Complex/Fraction, so they routed to builtin_ref_obj
--     which tried to read a 1-Word Obj* arg. But Complex/Fraction are
--     2-word native at builtin boundaries since Phase 4f.

struct Pt { x Int; y Int }

-- --- Map[K, V] with inline composite V ---
let m1 = [1: 1.0+2.0i, 2: 3.0+4.0i];
m1[1] println;
m1[99] println;
get(m1, 1) println;
get(m1, 99) println;
m1[1] unwrap println;
m1[99] unwrapOr(0.0+0.0i) println;
get(m1, 1, 0.0+0.0i) println;
get(m1, 99, 0.0+0.0i) println;

let m2 = [1: 1/2, 2: 3/4];
m2[1] println;
m2[1] unwrap println;
m2[99] unwrapOr(0/1) println;

let m6 = [1: (10, 20), 2: (30, 40)];
m6[1] println;
m6[1] unwrap println;
m6[99] unwrapOr((0, 0)) println;

let m7 = [1: Pt{x:1, y:2}, 2: Pt{x:3, y:4}];
m7[1] println;
m7[1] unwrap println;
m7[99] unwrapOr(Pt{x:0, y:0}) println;

-- --- Map[K, V] with inline composite K ---
let mck = [(1.0+2.0i): 10, (3.0+4.0i): 20];
mck[1.0+2.0i] println;
let mfk = [(1/2): 10, (3/4): 20];
mfk[1/2] println;
let mtk = [(1, 2): 10, (3, 4): 20];
mtk[(1, 2)] println;

-- --- pairs() with inline composite tuple element ---
-- These used to print correctly but length==0, [0] crashed, @ produced nothing.
let ps1 = m1 pairs;
ps1 println;
ps1 length println;
ps1[0] println;
ps1 @ println;

let ps6 = m6 pairs;
ps6 println;
ps6 length println;
ps6 @ println;

-- pairs() with heap tuple (too big for Inline) still uses ObjArray
struct Big { a Int; b Int; c Int; d Int; e Int }
let mbig = [1: Big{a:1, b:2, c:3, d:4, e:5}];
let psbig = mbig pairs;
psbig println;
psbig length println;

-- --- Set[T] with inline composite T ---
let s1 = Set(1.0+2.0i, 3.0+4.0i);
contains(s1, 1.0+2.0i) println;
contains(s1, 5.0+6.0i) println;
s1 toArray println;

let s2 = Set(1/2, 3/4);
contains(s2, 1/2) println;
s2 toArray println;

let s3 = Set((1, 2), (3, 4));
contains(s3, (1, 2)) println;
s3 toArray println;

-- --- zip(Array, Array) with Complex/Fraction elements ---
let xs = [1, 2, 3];
let cs = [1.0+2.0i, 3.0+4.0i, 5.0+6.0i];
zip(xs, cs) println;
zip(xs, cs) @ println;

let fs = [1/2, 3/4, 5/6];
zip(xs, fs) println;
zip(xs, fs) @ println;

zip(cs, fs) println;

-- --- enumerate(Array) with Complex/Fraction elements ---
enumerate(cs) println;
enumerate(cs) @ println;
enumerate(fs) println;

-- --- zip(List, List) with Complex/Fraction ---
let xsL = List(1, 2, 3);
let csL = List(1.0+2.0i, 3.0+4.0i, 5.0+6.0i);
zip(xsL, csL) println;
zip(xsL, csL) @ println;

let fsL = List(1/2, 3/4, 5/6);
zip(xsL, fsL) println;

-- --- enumerate(List) with Complex/Fraction ---
enumerate(csL) println;
enumerate(fsL) println;

-- --- Ref[Complex] / Ref[Fraction] (used to segfault) ---
var rc = ref(1.0+2.0i);
rc deref println;
rc setref(7.0+8.0i);
rc deref println;
setref(rc, 9.0+10.0i);
rc deref println;

var rf = ref(1/2);
rf deref println;
rf setref(7/8);
rf deref println;
setref(rf, 9/10);
rf deref println;

-- Ref[Tuple] / Ref[Struct] still work
var rt = ref((1, 2));
rt deref println;
rt setref((3, 4));
rt deref println;

var rp = ref(Pt{x:1, y:2});
rp deref println;
rp setref(Pt{x:3, y:4});
rp deref println;

-- Phase 4g.24: Map subscript with Inline Option result.
-- The opcode used to produce a heap Enum* and rely on emitUnboxIfInline
-- at each call site to unbox into the multi-word slot. The list-index
-- auto-map path (m[idxList]) didn't unbox at all -- the consed value was
-- a heap Enum* stuffed into a 1-word list head slot that the consumer
-- read as a (broken) Inline Option, printing 'Option<Complex>.?' with a
-- garbage discriminant.

-- --- m[idxList] now writes Inline Option natively into the list head ---
let mc = [1: 1.0+2.0i, 2: 3.0+4.0i];
let idxL = List(1, 2, 99);
mc[idxL] println;
mc[idxL] length println;
mc[idxL] @ println;

let mf = [1: 1/2, 2: 3/4];
mf[idxL] println;
mf[idxL] @ println;

let mt = [1: (10, 20), 2: (30, 40)];
mt[idxL] println;
mt[idxL] @ println;

let mp = [1: Pt{x:1, y:2}, 2: Pt{x:3, y:4}];
mp[idxL] println;
mp[idxL] @ println;

-- --- m[idxArr] (array index path) ---
let idxA = [1, 2, 99];
mc[idxA] println;
mf[idxA] println;
mt[idxA] println;
mp[idxA] println;

-- --- match on m[k] directly (Inline Option path) ---
match (mc[1]) {
    Option.some(c): c println;
    Option.none: "none" println;
}
match (mc[99]) {
    Option.some(c): c println;
    Option.none: "none" println;
}
match (mt[1]) {
    Option.some(t): t println;
    Option.none: "none" println;
}
match (mp[1]) {
    Option.some(p): p println;
    Option.none: "none" println;
}

-- --- Heap Option (V too big for Inline) still works ---
struct Big { a Int; b Int; c Int; d Int; e Int }
let mbig = [1: Big{a:1, b:2, c:3, d:4, e:5}];
mbig[1] println;
mbig[99] println;
mbig[idxL] println;
mbig[idxA] println;
