-- Type inference

-- Infer Int
let a = 42;
a println;

-- Infer Float
let b = 3.14;
b println;

-- Infer String
let c = "hello";
c println;

-- Infer Bool
let d = true;
d println;

-- Infer from expression
let e = 1 + 2;
e println;

-- Infer array type
let f = [1, 2, 3];
f println;

-- Infer tuple type
let g = (1, "hello", 3.14);
g println;

-- Infer from function return
fn square(x Int) = x * x;
let h = square(5);
h println;

-- Infer lambda return type
let double = fn(x Int) { x * 2 };
7 double println;

-- Infer from ternary
let i = true ? 1 : 2;
i println;

-- Infer struct type
struct Point { x Float, y Float }
let p = Point { x: 1.0, y: 2.0 };
p println;

-- Infer from function call
fn add(a Int, b Int) Int = a + b;
let j = add(3, 4);
j println;
