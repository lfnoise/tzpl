-- A declaration inside a function body is local, even when a global of the
-- same name exists. (A function body's outermost scope used to count as top
-- level, so its `let n = 20` also stored 20 into the global `n`'s slot; when
-- the global held an object the GC then followed 20 as a pointer and crashed.)

let n = "global n";
var m = [1, 2];
let (p, q) = ("global p", "global q");
var tag = [9];

fn plain() Int {
    let n = 20;
    var m = 7;
    let (p, q) = (1, 2);
    var tag = 3;
    tag = tag + 1;
    n + m + p + q + tag
}

let lam = fn(x Int) Int { let n = x; var m = x * 2; n + m };
let tmpl = fn(x) { let n = x; n };

coro fn counter() Int {
    let n = 5;
    var m = 1;
    while (m <= n) { yield m; m = m + 1; }
}

plain() println;
lam(10) println;
tmpl(4) println;
counter() toList println;

-- Allocate enough to run full GC cycles over the globals.
var acc = 0;
for (i : (1..200000)) { let a = [i, i + 1] map(fn(v Int) Int { v * 2 }); acc = acc + a[0]; }
acc println;

n println;
m println;
p println;
q println;
tag println;
