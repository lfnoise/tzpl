-- QA: Tuple operations

-- Tuple concatenation
println((1, 2) $ (3, 4));
println((1, "hello") $ (true, 3.14));

-- Single element tuple
(42,) println;
(42,).0 println;

-- Empty tuple
() println;

-- Tuple index access
let t = (10, "hello", true);
t.0 println;    -- 10
t.1 println;    -- hello
t.2 println;    -- true

-- Nested tuple
let nested = ((1, 2), (3, 4));
nested.0 println;   -- (1, 2)
nested.1 println;   -- (3, 4)
nested.0.0 println;  -- 1
nested.1.1 println;  -- 4

-- Tuple equality
println((1, 2, 3) == (1, 2, 3));    -- true
println((1, 2, 3) != (1, 2, 4));    -- true
println((1, "a") == (1, "a"));      -- true
println((1, "a") == (1, "b"));      -- false

-- Tuple in array
let tuples = [(1, "a"), (2, "b"), (3, "c")];
tuples.0 println;  -- [1, 2, 3]
tuples.1 println;  -- [a, b, c]

-- Tuple destructuring
let (a, b, c2) = (10, 20, 30);
a println;   -- 10
b println;   -- 20
c2 println;  -- 30

-- Tuple as function return
fn divmod(x Int, y Int) (Int, Int) = (x // y, x % y);
divmod(17, 5) println;       -- (3, 2)
let (q, r) = divmod(17, 5);
q println;  -- 3
r println;  -- 2
