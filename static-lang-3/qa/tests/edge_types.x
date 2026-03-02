-- QA: Edge cases in type system

-- Numeric promotion: Int -> Float
let x = 1 + 2.0;
x println;

-- Numeric promotion: Int -> Fraction
let f = 1 / 3;
f println;

-- Numeric promotion: Fraction -> Float
let g = 1/3 + 0.5;
g println;

-- Numeric promotion chain: Int -> Fraction -> Float
let h = 1 / 3 * 1.0;
h println;

-- Int -> Complex
let z = 1 + 2i;
z println;

-- Float -> Complex
let w = 1.0 + 2i;
w println;

-- Fraction -> Complex (should promote to float first)
-- let fc = 1/3 + 1i;
-- fc println;

-- Type annotation matching
let a Int = 42;
let b Float = 3.14;
let c String = "hello";
let d Bool = true;
a println;
b println;
c println;
d println;

-- Array type inference
let arr = [1, 2, 3];  -- [Int]
arr println;

let arr2 = [1.0, 2.0, 3.0];  -- [Float]
arr2 println;

-- Mixed numeric in array - should promote?
-- let arr3 = [1, 2.0, 3];  -- should this work?

-- Tuple with mixed types
let tup = (1, "hello", 3.14, true);
tup println;
tup.0 println;
tup.1 println;
tup.2 println;
tup.3 println;

-- Template type inference
fn identity<T>(x T) T = x;
identity(42) println;
identity("hello") println;
identity(3.14) println;
identity(true) println;

-- Template with multiple type params
fn make_pair<A, B>(a A, b B) (A, B) = (a, b);
make_pair(1, "hello") println;
make_pair(true, 3.14) println;

-- Template with array
fn first<T>(arr [T]) T = arr[0];
first([1, 2, 3]) println;
first(["hello", "world"]) println;
first([true, false]) println;

-- Bool comparisons
println(true == true);
println(true == false);
println(false == false);
println(true != false);

-- Void function
fn side_effect() Void {
    42;  -- value is discarded
}
side_effect();
"after side_effect" println;

-- Printing different types
println(42);
println(3.14);
println("hello");
println(true);
println(false);
println(1/3);
println([1, 2, 3]);
println((1, 2, 3));
println(List(1, 2, 3));
