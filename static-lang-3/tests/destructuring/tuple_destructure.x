-- Tuple destructuring

-- Basic pair
let (x, y) = (1, 2);
x println;
y println;

-- Triple
let (a, b, c) = (10, 20, 30);
a println;
b println;
c println;

-- With rest
let (t1, t2, ...trest) = (10, 20, 30, 40, 50);
t1 println;
t2 println;
trest println;

-- Discard rest
let (tf, ...) = (100, 200, 300);
tf println;

-- Rest with 1 remaining -> 1-tuple
let (r1a, ...r1b) = (1, 2);
r1b println;

-- Rest with 0 remaining -> ()
let (r0a, ...r0b) = (1,);
r0b println;

-- var destructuring
var (mx, my) = (100, 200);
mx println;
my println;
mx = 999;
mx println;

-- const destructuring
const (cx, cy) = (42, 84);
cx println;
cy println;
