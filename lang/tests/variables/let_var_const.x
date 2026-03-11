-- Variable declarations: let, var, const

-- let (immutable)
let x = 42;
x println;

let y = 3.14;
y println;

let s = "hello";
s println;

let b = true;
b println;

-- var (mutable)
var m = 10;
m println;
m = 20;
m println;
m = m + 5;
m println;

-- var with different types
var vi = 0;
vi = 42;
vi println;

var vf = 0.0;
vf = 3.14;
vf println;

var vs = "";
vs = "hello";
vs println;

var vb = false;
vb = true;
vb println;

-- const (immutable, constant)
const PI = 3.14159;
PI println;

const MAX = 100;
MAX println;

-- Type annotations
let n Int = 42;
n println;

let f Float = 2.5;
f println;

let str String = "world";
str println;
