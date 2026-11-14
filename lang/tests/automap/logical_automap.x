-- Element-wise `&&` / `||`. With an `@` operand, each element pair used to
-- go through an integer add (true && true = 2, false && true = 1), so `&&`
-- came out all-true; without `@`, a collection operand typed as a scalar
-- Bool and tested the array pointer. Both now map like comparisons do, over
-- arrays, persistent vectors, lists, `@@`, and cartesian `@1`/`@2`. The
-- scalar forms still short-circuit (`tick` never prints).
let bs = [true, false];
let fs = [false, false];
println(bs @ && true);
println(true && bs @);
println(bs @ && [true, true] @);
println(fs @ || false);
println(bs @ || fs @);
println(bs && true);
println(fs || bs);
println(#[true, false] @ && true);
println(bs toList @ && true);
println([[true, false], [true]] @@ && true);
println(bs @1 && [true, false] @2);
println([1, 0] @ && 1);
println(true && false);
println(1 && 2);
fn tick() Bool { println("tick"); true }
println(false && tick());
println(true || tick());
