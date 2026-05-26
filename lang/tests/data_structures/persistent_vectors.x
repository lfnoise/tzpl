-- Persistent vector literals and operations (#[...])

let v = #[1, 2, 3];
v println;
v[0] println;
v[2] println;
v[-1] println;            -- cyclic indexing
v length println;

-- push returns a NEW vector; the original is unchanged
let v2 = v push(4);
v2 println;
v println;
v2 length println;

-- put replaces an element by index, returning a new vector
let v3 = v put(1, 99);
v3 println;
v println;

-- numeric promotion in a literal
let mixed = #[1, 2.0, 3];
mixed println;

-- empty vector (element type from annotation)
let e #[Int] = #[];
e println;
e length println;

-- equality is structural
(#[1, 2, 3] == #[1, 2, 3]) println;
(#[1, 2, 3] == #[1, 2, 4]) println;
(v == v2) println;

-- concatenation with $
(#[1, 2] $ #[3, 4, 5]) println;

-- for-loop iteration
var total = 0;
for (x : v2) { total = total + x; }
total println;

-- larger vector exercises the trie tree (beyond the 32-element tail)
var big = #[0];
var i = 1;
while (i < 100) { big = big push(i * i); i = i + 1; }
big length println;
big[10] println;          -- 100
big[99] println;          -- 9801
var ok = true;
var j = 0;
while (j < 100) { if (big[j] != j * j) { ok = false; } j = j + 1; }
ok println;
