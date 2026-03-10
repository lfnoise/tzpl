-- QA: Implicit comparison auto-mapping (previously caused SEGFAULT, now fixed)

fn gt(x Int, threshold Int) Bool = x > threshold;
fn lt(x Int, threshold Int) Bool = x < threshold;
fn eq(x Int, val Int) Bool = x == val;

-- Function auto-mapping:
fn is_big(x Int) Bool = x > 2;
[1, 5, 3] is_big println;

-- Explicit @:
println([1, 5, 3] @ > 2);
println([1, 5, 3] @ < 2);
println([1, 5, 3] @ >= 5);
println([1, 5, 3] @ == 3);
println([1, 5, 3] @ != 3);

-- Implicit auto-mapping (previously crashed):
println([1, 2, 3] > 1);
println([1, 2, 3] < 2);
println([1, 2, 3] >= 2);
println([1, 2, 3] <= 2);
println([1, 2, 3] == 2);
println([1, 2, 3] != 2);
println((1, 2, 3) > 1);
println([1.0, 2.0] > 1.5);
println(List(1, 2, 3) > 2);

-- Scalar on left:
println(2 > [1, 2, 3]);
println(2 < [1, 2, 3]);
