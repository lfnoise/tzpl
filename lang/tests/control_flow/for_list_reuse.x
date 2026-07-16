-- Regression: `for (x : xs)` over a List must not consume xs.
-- The loop's tail-advance used to run on the variable's own register,
-- leaving the list nil after the loop (and head-of-nil crashed).

let xs = List(1, 2, 3);
for (i : xs) { i println; }
xs length println;
xs head println;

-- same through a lazy coroutine-backed list
coro fn gen() Int {
    yield 7;
    yield 8;
}
let ys = gen() toList;
for (i : ys) { i println; }
ys length println;
ys head println;

-- multi-word inline tuple elements: head spans two registers; re-reads of
-- memoized nodes after a full drain must still see both words
coro fn gp() (Float, Int) {
    yield (0.5, 40);
    yield (1.5, 41);
}
let zs = gp() toList;
for (tc : zs) { tc.0 println; }
zs length println;
let h = zs head;
h.0 println;
h.1 println;

-- nested loops over the same list
let ns = List(1, 2);
for (a : ns) {
    for (b : ns) {
        "%^%^" fmt(a, b) println;
    }
}
ns length println;
