-- QA: Test struct inheritance and methods

-- From CLAUDE.md: "Structs can inherit from another struct"
-- "Method: A function which dispatches on the type of the receiver argument"
-- "Methods are not declared inside of the struct"
-- "The receiver argument is not in the argument list"
-- "The receiver is referred to as `this` within the method"

-- Let's see if methods work
struct Point { x Float, y Float }

-- Method on Point (does this syntax work?)
-- fn Point.magnitude() Float = sqrt(this.x * this.x + this.y * this.y);

-- Or is it done differently?
-- Let's test regular functions first
fn magnitude(p Point) Float = sqrt(p.x * p.x + p.y * p.y);
magnitude(Point{3.0, 4.0}) println;  -- 5.0

-- Space pipeline to mimic methods
Point{3.0, 4.0} magnitude println;  -- 5.0

-- Operator overloading as alternative to methods
fn +(a Point, b Point) Point = Point { x: a.x + b.x, y: a.y + b.y };
fn -(a Point, b Point) Point = Point { x: a.x - b.x, y: a.y - b.y };
fn *(s Float, p Point) Point = Point { x: s * p.x, y: s * p.y };

let p1 = Point{1.0, 2.0};
let p2 = Point{3.0, 4.0};
println(p1 + p2);
println(p2 - p1);
println(2.0 * p1);

-- Struct with many fields
struct Config {
    width Int,
    height Int,
    title String,
    fullscreen Bool
}

let cfg = Config { width: 800, height: 600, title: "Test", fullscreen: false };
cfg.width println;
cfg.height println;
cfg.title println;
cfg.fullscreen println;

-- Struct update on multi-field struct
let cfg2 = Config { ...cfg, width: 1920, height: 1080 };
cfg2.width println;
cfg2.height println;
cfg2.title println;  -- inherited from cfg
cfg2.fullscreen println;  -- inherited from cfg
