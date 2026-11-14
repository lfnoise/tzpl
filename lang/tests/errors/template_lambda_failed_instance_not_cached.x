-- @repl
-- A template-lambda instantiation whose body fails to type-check must not be
-- cached: the REPL discards the failed statement, and a re-run used to hit
-- the cached instantiation, skip the body check, and run the ill-typed body
-- (here an `if` on a [Bool] took the `then` branch for every element).
let f = fn(j) { if (j > 0.0) { 0.5 * j / 9 } else { 0 } };
let x = [1.0, -1.0];
f(x);
f(x);
