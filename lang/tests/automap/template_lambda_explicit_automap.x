-- Explicit `@` on a call of a template (untyped-parameter) lambda maps the
-- lambda over the elements: it is instantiated at the element type, not the
-- collection type. It used to be instantiated at the whole array, so a body
-- that branches on its parameter failed with "If condition must be bool or
-- int" (and, in the REPL, a re-run then silently took the `then` branch).
let f = fn(j) { if (j > 0.0) { 0.5 * j / 9 } else { 0 } };
let x = [1.0, -1.0];
println(x @ f);
println(f(x @));
println(f(-1.0));

-- Nested depth, persistent vector, and lazy list collections.
println([[1.0, -1.0], [2.0]] @@ f);
println(#[1.0, -1.0] @ f);
let xs = [1.0, -1.0, 2.0] toList;
println(xs @ f);

-- Multi-argument: one mapped, one scalar; zip.
let h = fn(a, b) { a + b };
println([1, 2] @ h(10));
println([1, 2] @ h([10, 20] @));

-- Cartesian `@1`/`@2` on lambda values nests one output level per
-- dimension (it used to zip silently), for typed and template lambdas, in
-- either argument order, over arrays and persistent vectors.
let ht = fn(a Int, b Int) Int { a + b };
println(ht([1, 2] @1, [10, 20, 30] @2));
println([1, 2] @1 h([10, 20, 30] @2));
println(h([10, 20, 30] @2, [1, 2] @1));
println(h([1, 2] @, [10, 20, 30] @2));
println(h(#[1, 2] @1, [10, 20] @2));
println(h([1.0, -1.0] @1, [0.5, 2.0] @2));
let pv = fn(a Int, b Int) { println(a * b); };
pv([1, 2] @1, [10, 20] @2);

-- Void-returning template lambda under `@` runs once per element.
let p = fn(v) { println(v * 2); };
[3, 4] @ p;

-- A template lambda value called through an expression callee.
let fs = [fn(j) { j * 3 }];
println(fs[0]([1, 2] @));

-- An instantiation at the element type is shared with direct scalar calls.
println(f(2.0));
