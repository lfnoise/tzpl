-- A module-level function's body is type-checked on demand -- a template
-- when first instantiated, a function without a declared return type when
-- first called -- and that first call may be inside another function. The
-- body must not see that caller's locals. (It did: a caller's parameter
-- named `chans` made `sh chans` in the body a call of an Int, which with
-- any `call` overload declared was a type error.)

struct Shape { dims [Int] }
fn chans(s Shape) Int = s.dims length;
struct Dv { k Int }
fn call(d Dv, i Int) Int = d.k + i;      -- callable objects exist

constraint AsShape = Shape | [Int];
fn asShape(s Shape) Shape = s;
fn asShape(dims [Int]) Shape = Shape { dims: dims };

-- A template instantiated first from a caller with a local named `chans`.
fn fill(chans Int, x Int) [Int] = fill(Shape { dims: [1, chans] }, x);
fn fill(a AsShape, x Int) [Int] {
	let sh = a asShape;
	x repeat(sh chans)
}
fill(3, 7) println;            -- [7, 7]
fill([1, 2, 3], 5) println;    -- [5, 5, 5]

-- A function with an inferred return type, first called from such a caller.
fn g(chans Int) Int = count(Shape { dims: [1, 2, 3] }) + chans;
fn count(sh Shape) = sh chans;
g(10) println;                 -- 13

-- A template first instantiated inside a lambda: its own locals are not
-- the lambda's captures.
fn twiceLen(a AsShape) Int {
	let sh = a asShape;
	let n = sh chans;
	n * 2
}
let f = fn(k Int) Int { twiceLen([1, 2, 3]) + k };
f(100) println;                -- 106
