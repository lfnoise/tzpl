-- Integer-only binary operators auto-mapped over collections apply the
-- operator element-wise (they once fell back to "+"), at any nesting depth,
-- over tuple elements, and with implicit or explicit @.
println([5, 6] & 3);
println([5, 6] | 8);
println([5, 6] ^ 1);
println([1, 2] << 2);
println([-8, 16] >> 1);
println([-8, 16] >>> 60);
println(List(5, 6) & 3);
println([[5, 6], [7]] ^ 1);
println(3 & [5, 6]);
println([5, 6] & [3, 2]);
println([(5, 6)] ^ 1);
println([[5, 6]] % 4);
println([[-8]] >> 1);
println([true, false] % 2);
println([7, 9] % 4);
println([5, 6] @ & 3);
println([[5, 6]] @ & 3);
println([[5, 6]] @@ ^ 1);
println(#[5, 6] & 3);
let xs = [12, 10];
println(xs & 6);
println(xs % 5);

-- '//' auto-maps too, and is overloadable like the other operators.
struct P { x Int; }
fn //(a P, b Int) P = P { x: a.x // b };
println([7, 9] // 2);
println([[7], [9, -9]] // 2);
println(List(7, 9) // 2);
let ps = [P { x: 9 }, P { x: -9 }] // 2;
println(ps[1].x);
