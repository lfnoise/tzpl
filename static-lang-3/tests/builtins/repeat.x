-- Test repeat builtin function

-- repeat Int
let a = repeat(42, 5);
a println;
-- expect: [42, 42, 42, 42, 42]

-- repeat Float
let b = repeat(3.14, 3);
b println;
-- expect: [3.14, 3.14, 3.14]

-- repeat Bool
let c = repeat(true, 4);
c println;
-- expect: [true, true, true, true]

-- repeat String
let d = repeat("hello", 2);
d println;
-- expect: [hello, hello]

-- repeat with 0
let e = repeat(1, 0);
e println;
-- expect: []

-- repeat with negative n
let f = repeat(1, -3);
f println;
-- expect: []

-- repeat with 1
let g = repeat(99, 1);
g println;
-- expect: [99]

-- repeat Symbol
let h = repeat('foo, 3);
h println;
-- expect: ['foo, 'foo, 'foo]
