-- List tests

-- Literal
let xs = List(1, 2, 3);
xs println;

-- Cons operator
let ys = 0 :: xs;
ys println;

-- nil chain
let zs = 1 :: 2 :: 3 :: nil;
zs println;

-- Empty list with type annotation
let empty List<Int> = nil;
empty println;

-- Pattern matching: head :: tail
match (xs) {
    h :: t: {
        h println;
        t println;
    }
    nil: "empty" println;
}

-- Nested cons pattern
match (xs) {
    a :: b :: rest: {
        a println;
        b println;
        rest println;
    }
    _: "short" println;
}

-- List arithmetic (lazy)
println(List(1, 2, 3) + 10);
println(100 + List(1, 2, 3));
println(List(10, 20, 30) * 2);
println(List(10, 20, 30) - 5);

-- List + List (zip)
println(List(1, 2, 3) + List(10, 20, 30));

-- Negation
println(-List(1, 2, 3));

-- Concat
println(List(1, 2, 3) $ List(4, 5, 6));
println(List(1, 2) $ List(3, 4) $ List(5, 6));

-- Length
List(1, 2, 3, 4, 5) length println;
List(1, 2, 3) length println;
