-- Syntax that dispatches to an overloadable function (operators, x[i] -> at,
-- x[i] = v -> put!, value(args) -> call) reports "No matching overload" with
-- the built-in forms, the declared candidates, and a declaration hint --
-- not a message implying only the built-in form exists.
struct P { x Float; }
struct Q { y Float; }
fn +(a P, b P) P = P { x: a.x + b.x };
fn -(a P) P = P { x: 0.0 - a.x };
fn <(a P, b P) Bool = a.x < b.x;
let p = P { x: 1.0 };
let q = Q { y: 2.0 };
var pv = p;
let r = &pv;
println(p + q);
println(-q);
println(p < 3);
println(p == q);
println(q % 2);
println(q & 1);
println(q $ 1);
r <- q;
p <- q;
println(q[0]);
var qq = Q { y: 3.0 };
qq[0] = 1.0;
println(q(1));
println(Q { y: 1.0 }(1));
