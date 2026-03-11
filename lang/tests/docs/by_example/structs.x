-- Structs from Tzopilotl by Example

struct Point {
    x Float,
    y Float
}

struct Person {
    name String,
    age Int
}

-- Named construction
let p = Point { x: 3.0, y: 4.0 };
let alice = Person { name: "Alice", age: 30 };

-- Positional construction
let p2 = Point{3.0, 4.0};
p2.x println;

-- Field access
p.x println;
p.y println;
alice.name println;
alice.age println;

-- Nested structs
struct Line {
    start Point,
    end Point
}

let line = Line { start: p, end: Point { x: 6.0, y: 8.0 } };
line.start.x println;
line.end.y println;

-- Print structs
p println;
alice println;
