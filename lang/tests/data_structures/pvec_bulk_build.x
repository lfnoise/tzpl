-- PVec::fromWords is a bulk (transient-style) builder: it fills full
-- 32-element leaves directly and groups them bottom-up into the trie instead
-- of doing `count` immutable pushes (each re-copying the growing tail). This
-- exercises the resulting structure across the trie-depth boundaries (32, 1024)
-- so a mis-computed shift_ or leaf grouping would surface as a bad element or
-- length. Every builder entry point below funnels through fromWords:
--   toPersistentVector, auto-map, and the map/filter builtins.
fn add1(x Int) Int = x + 1;
fn isOdd(x Int) Bool = (x % 2) == 1;

-- Build #[0..n-1] (array -> pvec), auto-map +1, and confirm every element and
-- the length. Returns a single aggregate flag so the golden stays small.
fn check(n Int) Bool {
    var a = [0];
    var i = 1;
    while (i < n) { a push!(i); i = i + 1; }
    let pv = a toPersistentVector;     -- bulk fromWords
    let m = pv add1;                   -- auto-map -> bulk fromWords
    var ok = (m length) == n;
    var j = 0;
    while (j < n) { if (m[j] != j + 1) { ok = false; } j = j + 1; }
    ok
}
check(1) println;        -- tail only
check(31) println;       -- tail only
check(32) println;       -- full tail, empty tree
check(33) println;       -- one tree leaf + tail (shift 5)
check(64) println;
check(1024) println;     -- 32 tree leaves, root still shift 5
check(1025) println;     -- tree grows to shift 10
check(2000) println;

-- map / filter builtins build their result with one bulk fromWords.
var b = [0]; var k = 1;
while (k < 200) { b push!(k); k = k + 1; }
let big = b toPersistentVector;        -- #[0..199]
let mp = big map(add1);
((mp length) == 200) println;
(mp[0] == 1) println;
(mp[199] == 200) println;
let fp = big filter(isOdd);
((fp length) == 100) println;
(fp[0] == 1) println;
(fp[99] == 199) println;
