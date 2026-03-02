-- Map literals and operations

let m = ["x": 1, "y": 2, "z": 3];
m println;
m["x"] unwrap println;
m["y"] unwrap println;
m length println;
m contains("x") println;
m contains("w") println;

-- put returns new map
let m2 = m put("w", 4);
m2 length println;
m2["w"] unwrap println;

-- remove returns new map
let m3 = m2 remove("y");
m3 contains("y") println;
m3 length println;

-- get with default
get(m, "x") unwrap println;
get(m, "missing", 99) println;

-- keys and values
m keys() length println;
m values() length println;

-- merge
let merged = merge(["a": 1], ["a": 2, "b": 3]);
merged["a"] unwrap println;
merged["b"] unwrap println;
merged length println;

-- integer key map
let intMap = [1: "one", 2: "two", 3: "three"];
intMap[2] unwrap println;
intMap length println;

-- tuple keys
let tm = [(1, 2): "a", (3, 4): "b", (5, 6): "c"];
tm[(1, 2)] unwrap println;
tm[(3, 4)] unwrap println;
tm[(5, 6)] unwrap println;
tm length println;
tm contains((1, 2)) println;
tm contains((7, 8)) println;

-- set keys
let sm = [Set(1, 2): "x", Set(3, 4): "y"];
sm[Set(2, 1)] unwrap println;
sm[Set(1, 2, 2, 1)] unwrap println;
sm[Set(4, 3)] unwrap println;
sm length println;
sm contains(Set(1, 2)) println;
sm contains(Set(5, 6)) println;

-- struct keys
struct Key { a Int, b Int }
let km = [Key { a: 1, b: 2 }: 10, Key { a: 3, b: 4 }: 20];
km[Key { a: 1, b: 2 }] unwrap println;
km[Key { a: 3, b: 4 }] unwrap println;
km length println;
km contains(Key { a: 1, b: 2 }) println;
km contains(Key { a: 5, b: 6 }) println;
