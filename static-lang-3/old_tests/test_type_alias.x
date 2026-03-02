-- Basic type alias for a primitive
type Natural = Int;
let n Natural = 42;
println(n);

-- Type alias for a generic type
type IntList = List<Int>;
let xs IntList = List(1, 2, 3);
println(xs);

-- Type alias for array type
type IntArray = [Int];
let arr IntArray = [10, 20, 30];
println(arr);

-- Type alias for tuple type
type Point = (Float, Float);
let p Point = (1.0, 2.0);
println(p);

-- Type alias for function type
type IntToInt = (Int) Int;
fn apply(f IntToInt, x Int) Int = f(x);
let result = apply(fn(x Int) Int { x * 2 }, 5);
println(result);

-- Type alias used in function parameters
type IntPair = (Int, Int);
fn add_pair(p IntPair) Int = p.0 + p.1;
println(add_pair((3, 4)));

-- Type alias for a struct
struct Vec2 { x Float, y Float }
type Vector = Vec2;
let v = Vector { x: 1.0, y: 2.0 };
println(v);

-- Type alias for a template struct
struct Box<T> { value T }
type IntBox = Box<Int>;
let b IntBox = Box { 42 };
println(b.value);

-- Alias is transparent: can use original type where alias is expected
let b2 Box<Int> = b;
println(b2.value);

-- Generic type alias
type Pair<T> = (T, T);
let ip Pair<Int> = (1, 2);
println(ip);

let fp Pair<Float> = (3.14, 2.72);
println(fp);

-- Generic type alias with multiple type params
type Map2<K, V> = (K, V, K, V);
let m Map2<String, Int> = ("a", 1, "b", 2);
println(m);

-- Generic type alias for array
type Vec<T> = [T];
let v2 Vec<Int> = [1, 2, 3];
println(v2);

-- Generic type alias in function signatures
type Wrapper<T> = Box<T>;
fn unwrap<T>(w Wrapper<T>) T = w.value;
let w = Box { "hello" };
println(unwrap(w));
