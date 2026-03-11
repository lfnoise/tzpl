-- Destructuring from Language X by Example

-- Tuple destructuring
let (x, y) = (1, 2);
x println;
y println;

let (a, b, c) = (10, 20, 30);
a println;
b println;
c println;

-- Struct destructuring
struct Point { x Float, y Float }
let Point { x: px, y: py } = Point { x: 3.0, y: 4.0 };
px println;
py println;

-- Array destructuring
let [d, e, f] = [100, 200, 300];
d println;
e println;
f println;

-- Array head/rest
let [head, ...tail] = [1, 2, 3, 4];
head println;
tail println;

-- Discard rest
let [first, ...] = [10, 20, 30];
first println;

-- Tuple head/rest
let (t1, t2, ...trest) = (10, 20, 30, 40, 50);
trest println;

-- Rest can capture any number of remaining elements (including 0 or 1)
let (a2, ...r1) = (1, 2);
r1 println;

let (b2, ...r0) = (1,);
r0 println;

-- Tuple struct destructuring
struct Point2(Float, Float);
let Point2(tx, ty) = Point2(3.0, 4.0);
tx println;
ty println;

-- Works with var and const too
var (mx, my) = (100, 200);
mx println;
my println;
mx = 999;
mx println;

const (cx, cy) = (42, 84);
cx println;
cy println;
