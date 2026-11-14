-- @repl
-- An input that fails to type-check never runs, so its bindings must not
-- outlive it. They used to stay declared over unset slots: `g` below was a
-- null lambda (calling it segfaulted), `a` read 0, and a failed redefinition
-- of `x` retyped it as a lambda over the old Int slot. Each follow-up line
-- now sees the state before the failed input: `g` and `a` are undeclared
-- (one error each), `x` is still the Int 5 (so `x + 1` is fine), and an
-- import from a failed input stays usable.
let f = fn(x) { x + 1 };
let g = f @;
1 g println;
let a = 5; let b = 1 + "x";
a println;
let x = 5;
let x = fn(j) { j } @;
(x + 1) println;
import std.strings.*; let c = 1 + "x";
"ab" parens println;
