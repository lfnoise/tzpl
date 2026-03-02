-- Tuple Structs from Language X by Example

-- Tuple struct declaration
struct Point(Float, Float);
struct Temperature(Float);

-- Construction uses function-call syntax
let p = Point(3.0, 4.0);
let temp = Temperature(98.6);

-- Field access by numeric index
p.0 println;
p.1 println;
temp.0 println;

-- Pattern matching on tuple structs
let Point(x, y) = p;
println(x + y);

match (temp) {
    Temperature(v): v println;
}

-- Printing
p println;
temp println;
