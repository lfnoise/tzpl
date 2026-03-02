-- Test Ref type

-- Basic ref creation and dereference
let x = &42;
*x println;

-- Ref mutation with <-
x <- 100;
*x println;

-- Ref with float
let y = &3.14;
*y println;
y <- 2.718;
*y println;

-- Ref with string
let s = &"hello";
*s println;
s <- "world";
*s println;

-- Ref with bool
let b = &true;
*b println;
b <- false;
*b println;

-- Print the ref itself
x println;

-- Ref with explicit type annotation
let r Ref<Int> = &0;
r <- 99;
*r println;

-- Nested operations
let a = &10;
let b2 = &20;
let sum = *a + *b2;
sum println;

-- Ref set returns the value
let v = (a <- 77);
v println;
*a println;
