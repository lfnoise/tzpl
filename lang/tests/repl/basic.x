-- @repl
-- Baseline REPL behaviour: statements print nothing, expressions print
-- "-> value : Type", and prints go to stdout in order.
let x = 10;
x + 1
x println;
fn twice(n Int) Int = n * 2;
twice(x)
var s = "a"; s = s $ "b"; s
[x, twice(x)]
