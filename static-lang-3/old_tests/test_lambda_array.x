-- Test arrays of lambdas (requires FunctionType interning)

-- Array of lambdas with same signature
let double = fn(x Int) Int { x * 2 };
let triple = fn(x Int) Int { x * 3 };
let negate = fn(x Int) Int { 0 - x };
let fns = [double, triple, negate];

-- Index and call
println(fns[0](5));
println(fns[1](5));
println(fns[2](5));

-- Lambdas with captures should also have same type
let a = 10;
let b = 20;
let addA = fn(x Int) Int { x + a };
let addB = fn(x Int) Int { x + b };
let adders = [addA, addB];
println(adders[0](5));
println(adders[1](5));

-- Mix of capturing and non-capturing lambdas
let mixed = [double, addA, negate];
println(mixed[0](7));
println(mixed[1](7));
println(mixed[2](7));
