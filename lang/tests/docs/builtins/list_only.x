-- List-only Functions from Builtin_Functions.html

-- head
println(List(1, 2, 3) head);
println(List(42) head);

-- tail
println(List(1, 2, 3) tail);
println(List(42) tail);

-- cons
println(cons(0, List(1, 2, 3)));
let empty List<Int> = nil;
println(cons(1, empty));

-- isNil, notNil
let xs = List(1, 2, 3);
println(isNil(xs));
println(isNil(empty));
println(notNil(xs));
println(notNil(empty));

-- cyc
println(List(1, 2, 3) cyc take(10));
println(List(1, 2, 3) cyc take(7));

-- ncyc
println(List(1, 2, 3) ncyc(3));
println(List(1, 2) ncyc(4));

-- hang
println(List(1, 2, 3) hang take(8));
println(List(10, 20) hang take(5));

-- iter
println(iter(1, fn(x Int) Int { x * 2 }) take(10));
println(iter(0, fn(x Int) Int { x + 1 }) take(5));
