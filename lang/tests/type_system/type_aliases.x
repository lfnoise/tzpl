-- Type aliases

-- Basic alias for primitive
type Natural = Int;
let n Natural = 42;
n println;

-- Alias for generic type
type IntList = List<Int>;
let xs IntList = List(1, 2, 3);
xs println;

-- Alias for array type
type IntArray = [Int];
let arr IntArray = [10, 20, 30];
arr println;

-- Alias for tuple type
type Point = (Float, Float);
let p Point = (1.0, 2.0);
p println;

-- Alias for function type
type IntToInt = (Int) Int;
fn apply(f IntToInt, x Int) Int = f(x);
let result = apply(fn(x Int) Int { x * 2 }, 5);
result println;

-- Alias used in function parameters
type IntPair = (Int, Int);
fn add_pair(p IntPair) Int = p.0 + p.1;
(3, 4) add_pair println;

-- Alias for struct
struct Vec2 { x Float, y Float }
type Vector = Vec2;
let v = Vector { x: 1.0, y: 2.0 };
v println;

-- Alias for template struct
struct Box<T> { value T }
type IntBox = Box<Int>;
let b IntBox = Box { 42 };
b.value println;

-- Transparency: use original type where alias expected
let b2 Box<Int> = Box { 99 };
let b3 IntBox = b2;
b3.value println;

-- Generic type alias
type Pair<T> = (T, T);
let ip Pair<Int> = (1, 2);
ip println;

let fp Pair<Float> = (3.14, 2.72);
fp println;

-- Generic alias for array
type Vec<T> = [T];
let v2 Vec<Int> = [1, 2, 3];
v2 println;

-- Generic alias in function signatures
type Wrapper<T> = Box<T>;
fn unwrap<T>(w Wrapper<T>) T = w.value;
let w = Box { "hello" };
w unwrap println;
