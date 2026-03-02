-- QA: Advanced Ref edge cases

-- Ref to Ref
let inner = &42;
let outer = &inner;
**outer println;       -- 42

-- Modify through nested ref
*outer <- 99;
*inner println;        -- should still be 42? or 99?

-- Ref to array
let arr_ref = &[1, 2, 3];
(*arr_ref) println;
arr_ref <- [4, 5, 6];
(*arr_ref) println;

-- Ref to tuple
let tup_ref = &(1, "hello");
(*tup_ref) println;
tup_ref <- (2, "world");
(*tup_ref) println;

-- Ref to struct
struct Point { x Int, y Int }
let pt_ref = &Point{1, 2};
(*pt_ref) println;
pt_ref <- Point{3, 4};
(*pt_ref) println;

-- Ref in a loop (counter pattern)
let counter = &0;
for (i : (1..5)) {
    counter <- *counter + i;
}
*counter println;     -- 15

-- Multiple refs, independent mutation
let a = &10;
let b = &20;
a <- *a + 5;
b <- *b + 5;
println(*a + *b);     -- 35

-- Ref equality (same ref)
let r = &42;
println(*r == *r);    -- true

-- Ref in struct field mutation
struct Container { value Ref<Int> }
let c = Container { value: &0 };
c.value <- 100;
*c.value println;     -- 100

-- Shared ref between structs
let shared = &0;
let c1 = Container { value: shared };
let c2 = Container { value: shared };
c1.value <- 42;
*c2.value println;    -- 42 (same ref)
