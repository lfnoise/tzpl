-- Basic template lambda
let f = fn<T>(x T) T { x + x };
f(3) println;
f(3.14) println;

-- With captures
let offset = 10;
let g = fn<T>(x T) T { x + offset };
g(5) println;
g(2.5) println;

-- Multiple calls to same template lambda (cache hit)
f(100) println;

-- With constraints
constraint Numeric = Int | Float;
let h = fn<T: Numeric>(x T) T { x * x };
h(4) println;
h(2.5) println;

-- Space-pipeline syntax with template lambdas
let double = fn<T>(x T) T { x + x };
42 double println;
3.14 double println;

-- Return type inference (omit return type)
let triple = fn<T>(x T) { x + x + x };
triple(7) println;
triple(1.5) println;
