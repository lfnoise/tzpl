-- Type Aliases from Tzopilotl by Example

-- Alias for a primitive type
type Natural = Int;
let n Natural = 42;
n println;

-- Alias for a collection type
type IntList = List<Int>;
let xs IntList = List(1, 2, 3);
xs println;

-- Alias for a tuple type
type Point = (Float, Float);
let p Point = (1.0, 2.0);
p println;

-- Alias for a function type
type IntToInt = (Int) Int;
fn apply(f IntToInt, x Int) Int = f(x);
let dbl = fn(x Int) Int { x * 2 };
apply(dbl, 5) println;

-- Alias for a struct type
struct Vec2 { x Float, y Float }
type Vector = Vec2;
let v = Vector { x: 1.0, y: 2.0 };
v println;

-- Alias for a template struct instantiation
struct Box<T> { value T }
type IntBox = Box<Int>;
let b IntBox = Box { 42 };
b println;

-- Generic type alias for a tuple
type Pair<T> = (T, T);
let ip Pair<Int> = (1, 2);
let fp Pair<Float> = (3.14, 2.72);
ip println;
fp println;

-- Multiple type parameters
type Entry<K, V> = (K, V);
let e Entry<String, Int> = ("age", 30);
e println;

-- Generic alias for an array
type Vec<T> = [T];
let va Vec<Int> = [1, 2, 3];
va println;

-- Generic alias wrapping a template struct
type Wrapper<T> = Box<T>;
fn unwrap<T>(w Wrapper<T>) T = w.value;
let bx = Box { value: 99 };
unwrap(bx) println;
