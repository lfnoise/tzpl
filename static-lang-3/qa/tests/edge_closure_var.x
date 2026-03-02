-- QA: Test whether closures capture var by value or reference

-- Closure over var captures by value at time of closure creation
var x = 10;
let f = fn() { x };
x = 20;
-- If captured by value: f() returns 10
-- If captured by reference: f() returns 20
f() println;

-- Closure over var in a loop - classic bug pattern
-- Each closure should capture the current loop value
var results = &[0, 0, 0];

-- Simple test: closure created after var mutation
var v = 1;
let c1 = fn() { v };
v = 2;
let c2 = fn() { v };
v = 3;
let c3 = fn() { v };

-- What do they return?
c1() println;
c2() println;
c3() println;

-- Ref captures work differently - they share state
let r = &10;
let g1 = fn() { *r };
r <- 20;
let g2 = fn() { *r };
r <- 30;

-- Both should return 30 (latest value)
g1() println;
g2() println;
