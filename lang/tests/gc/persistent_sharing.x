-- Structural sharing of persistent collections across GC traces.
--
-- Each `push`/`put` shares the unchanged parts of the previous version. We
-- retain several versions, force synchronous tracing cycles, and confirm that
-- every retained version stays intact and independent -- i.e. the collector
-- marks shared subtrees through whichever versions are still rooted, and never
-- frees a node a live version still points at.

-- Build three vector versions that share structure.
let v1 = #[10, 20, 30];
let v2 = v1 push(40);
let v3 = v2 push(50);

__gc_trace_cycle();

-- All three survive and are independent.
v1 length println;          -- 3
v2 length println;          -- 4
v3 length println;          -- 5
v1[0] println;              -- 10
v3[4] println;              -- 50
(v1 == #[10, 20, 30]) println;
(v3 == #[10, 20, 30, 40, 50]) println;

-- Drop v2/v3 implicitly by only using v1 below; trace again.
__gc_trace_cycle();
v1 length println;          -- 3 (still intact)
v1[2] println;              -- 30

-- Persistent maps share structure too.
let m1 = #['a: 1, 'b: 2];
let m2 = m1 put('c, 3);
__gc_trace_cycle();
m1 length println;          -- 2
m2 length println;          -- 3
m2['c] unwrap println;      -- 3
m1 contains('c) println;    -- false (m1 unchanged)

-- A snapshot kept across more updates remains valid after a trace.
let snap = m2;
let m3 = m2 put('d, 4) put('e, 5);
__gc_trace_cycle();
snap length println;        -- 3
m3 length println;          -- 5
snap contains('d) println;  -- false
m3['e] unwrap println;      -- 5

-- Build a large vector in a loop, driving the incremental auto-collector
-- (the loop accumulator is a live top-frame register across many cycles).
-- Exercises the trie tree past the 32-element tail and confirms every
-- shared subtree survives.
var big = #[0];
var n = 1;
while (n < 5000) { big = big push(n * n); n = n + 1; }
big length println;         -- 5000
var ok = true;
var p = 0;
while (p < 5000) { if (big[p] != p * p) { ok = false; } p = p + 1; }
ok println;                 -- true

-- Same for a persistent map built incrementally.
var pm = #[0: 0];
var q = 1;
while (q < 3000) { pm = pm put(q, q + 100); q = q + 1; }
pm length println;          -- 3000
pm[1500] unwrap println;    -- 1600
pm[2999] unwrap println;    -- 3099
