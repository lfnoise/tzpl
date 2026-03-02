-- QA: Type alias edge cases

-- Basic type alias
type IntList = [Int];
let x IntList = [1, 2, 3];
x println;
x length println;

-- Type alias for tuple
type Point2D = (Float, Float);
let p Point2D = (3.0, 4.0);
p println;
p.0 println;

-- Generic type alias
type Pair<T> = (T, T);
let ip Pair<Int> = (1, 2);
ip println;

let sp Pair<String> = ("hello", "world");
sp println;

-- Type alias for map
type Dict = [String: Int];
let d Dict = ["a": 1, "b": 2];
d length println;

-- Type alias used in function signature
fn sum_list(xs IntList) Int {
    var total = 0;
    for (x : xs) {
        total = total + x;
    }
    total
}
sum_list([1, 2, 3, 4, 5]) println;

-- Nested type alias
type Matrix = [[Int]];
let m Matrix = [[1, 2], [3, 4]];
m println;
m length println;
