-- Map Functions from Builtin_Functions.html

let m = ["a": 1, "b": 2, "c": 3];
m get("b") unwrap println;
m get("z", 99) println;

let m2 = ["a": 1];
let m3 = m2 put("b", 2);
m3 println;

let m4 = ["a": 1, "b": 2];
m4 contains("a") println;
m4 contains("z") println;

let m5 = ["x": 10, "y": 20];
m5 keys println;
m5 values println;

let a = ["x": 1, "y": 2];
let b = ["y": 99, "z": 3];
merge(a, b) println;
