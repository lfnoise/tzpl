-- Maps from Tzopilotl by Example

-- Create a map with [key: value] syntax
let ages = ["Alice": 30, "Bob": 25, "Carol": 35];
ages println;

-- Map type annotation: [K:V]
let scores [String:Int] = ["math": 95, "science": 88];
scores println;

-- The empty map takes its type from whatever surrounds it
let empty [Int:Int] = [:];
fn tally(m [Int:Int]) Int { m length }
[:] tally println;
tally([:]) println;
struct Counts { m [Int:Int] }
Counts { m: [:] } println;
[[1: 2], [:]] println;

-- Accessing values
let m = ["a": 1, "b": 2, "c": 3];
m get("b") unwrap println;
m get("z", 99) println;
m contains("a") println;
m contains("z") println;

-- Modifying maps (functional)
let m2 = ["a": 1];
let m3 = m2 put("b", 2);
m3 println;

let m4 = m3 remove("a");
m4 println;

-- merge combines two maps (second wins on conflicts)
let a = ["x": 1, "y": 2];
let b = ["y": 99, "z": 3];
merge(a, b) println;

-- Inspecting maps
let m5 = ["x": 10, "y": 20];
m5 length println;
m5 keys println;
m5 values println;
