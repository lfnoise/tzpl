-- QA: Function overload dispatch edge cases

-- Basic overloading by type
fn describe(x Int) String = "int: " $ toString(x);
fn describe(x Float) String = "float: " $ toString(x);
fn describe(x String) String = "string: " $ x;
fn describe(x Bool) String = "bool: " $ toString(x);

describe(42) println;
describe(3.14) println;
describe("hello") println;
describe(true) println;

-- Overloading by arity
fn f(x Int) Int = x;
fn f(x Int, y Int) Int = x + y;
fn f(x Int, y Int, z Int) Int = x + y + z;

f(1) println;        -- 1
f(1, 2) println;     -- 3
f(1, 2, 3) println;  -- 6

-- Overloading with default args
fn greet(name String, greeting String = "Hello") String = greeting $ ", " $ name $ "!";

greet("World") println;            -- Hello, World!
greet("World", "Hi") println;      -- Hi, World!

-- Overloading with struct types
struct Cat { name String }
struct Dog { name String }

fn speak(c Cat) String = c.name $ " says meow";
fn speak(d Dog) String = d.name $ " says woof";

speak(Cat{"Whiskers"}) println;
speak(Dog{"Rex"}) println;

-- Overloading with auto-map
fn negate(x Int) Int = -x;
fn negate(x Float) Float = -x;

[1, 2, 3] negate println;        -- [-1, -2, -3]
[1.0, 2.0, 3.0] negate println;  -- [-1.0, -2.0, -3.0]

-- Overloading with tuple vs single arg
fn wrap(x Int) (Int,) = (x,);
fn wrap(x Int, y Int) (Int, Int) = (x, y);

wrap(42) println;       -- (42,)
wrap(1, 2) println;     -- (1, 2)
