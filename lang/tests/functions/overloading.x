-- Function overloading

-- Overload by type
fn describe(x Int) String = "int";
fn describe(x Float) String = "float";
fn describe(x String) String = "string";
fn describe(x Bool) String = "bool";

42 describe println;
3.14 describe println;
"hello" describe println;
true describe println;

-- Overload by arity
fn sum(a Int) Int = a;
fn sum(a Int, b Int) Int = a + b;
fn sum(a Int, b Int, c Int) Int = a + b + c;

sum(5) println;
sum(3, 4) println;
sum(1, 2, 3) println;

-- Operator overloading
struct Vec2 { x Float, y Float }
fn +(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x + b.x, y: a.y + b.y };
fn -(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x - b.x, y: a.y - b.y };
fn *(s Float, v Vec2) Vec2 = Vec2 { x: s * v.x, y: s * v.y };

let v1 = Vec2 { x: 1.0, y: 2.0 };
let v2 = Vec2 { x: 3.0, y: 4.0 };
println(v1 + v2);
println(v2 - v1);
println(2.0 * v1);

-- Overloaded process function
fn process(x Int) String = "processed int";
fn process(x String) String = "processed string";

42 process println;
"hello" process println;

-- Non-template preferred over template
fn add(a Int, b Int) Int = a + b + 1000;
fn add<T>(a T, b T) T = a + b;
add(3, 4) println;
add(1.5, 2.5) println;
