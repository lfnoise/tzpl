-- A top-level `var` is one variable, shared by top-level code, functions
-- and closures. (Top-level code used to read and write a register copy,
-- so a function's writes were invisible at top level, a top-level write
-- was invisible to functions, and closures saw the initial value.)

var count = 0;
fn inc() Void { count = count + 1; }
fn get() Int = count;
inc();
inc();
count println;          -- 2: a function's writes, seen at top level
count = 10;
get() println;          -- 10: a top-level write, seen by a function

-- Closures created in a top-level loop share the var (as in a function).
var fs [() Int] = [];
var b = 0;
while (b < 3) {
	fs push!(fn() Int { b });
	b = b + 1;
}
for (f : fs) { f() println; }   -- 3 3 3

-- A `let` copy per pass gives each closure its own value.
var gs [() Int] = [];
var k = 0;
while (k < 3) {
	let k1 = k;
	gs push!(fn() Int { k1 });
	k = k + 1;
}
for (g : gs) { g() println; }   -- 0 1 2

-- A for-loop variable is a fresh binding each pass.
var hs [() Int] = [];
for (i : (0..2)) { hs push!(fn() Int { i }); }
for (h : hs) { h() println; }   -- 0 1 2

-- Destructured top-level vars are shared too.
var (p, q) = (1, 2);
fn sumPQ() Int = p + q;
p = 5;
sumPQ() println;        -- 7
