-- Constants test

-- Float constants
const ONE = 1.0;
ONE println;

const PI = 3.14159;
PI println;

const HALF = 0.5;
HALF println;

const ZERO = 0.0;
ZERO println;

const NEG = -1.0;
NEG println;

-- Integer constants
const MAX = 100;
MAX println;

const ZERO_INT = 0;
ZERO_INT println;

const NEG_INT = -42;
NEG_INT println;

-- Bool constants
const YES = true;
YES println;

const NO = false;
NO println;

-- String constants
const GREETING = "hello";
GREETING println;

-- Const used in expressions
const A = 2.0;
const B = 3.0;
const C = A + B;
C println;

const X = 10;
const Y = 20;
const Z = X * Y;
Z println;

-- Const inside a function
fn testConst() {
  const LOCAL = 99;
  LOCAL println;
  const LOCAL_F = 1.5;
  LOCAL_F println;
}
testConst();

-- Const with println function call syntax
const VAL = 7.0;
println(VAL);

-- Multiple consts used together
const R = 5.0;
const AREA = PI * R * R;
AREA println;
