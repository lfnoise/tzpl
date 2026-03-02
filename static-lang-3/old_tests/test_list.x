-- List literal
let xs = List(1, 2, 3);
println(xs);

-- Cons operator
let ys = 0 :: xs;
println(ys);

-- nil
let zs = 1 :: 2 :: 3 :: nil;
println(zs);

-- Empty list with type annotation
let empty List<Int> = nil;
println(empty);

-- Pattern matching
match (xs) {
    h :: t: {
        println(h);
        println(t);
    }
    nil: println("empty");
}

-- Nested cons pattern
match (xs) {
    a :: b :: rest: {
        println(a);
        println(b);
        println(rest);
    }
    _: println("short");
}
