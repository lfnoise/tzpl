-- Tuple structs

-- Basic tuple struct
struct Point(Float, Float);
let p = Point(1.0, 2.0);
p println;
p.0 println;
p.1 println;

-- Newtype
struct Temperature(Float);
let temp = Temperature(98.6);
temp println;
temp.0 println;

-- Pattern matching on tuple struct
let Point(x, y) = p;
println(x + y);

match (temp) {
    Temperature(v): v println;
}

-- Template tuple struct
struct Wrapper<T>(T);
let w = Wrapper(42);
w println;
w.0 println;

let ws = Wrapper("hello");
ws println;
ws.0 println;

-- Multi-field tuple struct
struct RGB(Int, Int, Int);
let color = RGB(255, 128, 0);
color println;
color.0 println;
color.1 println;
color.2 println;
