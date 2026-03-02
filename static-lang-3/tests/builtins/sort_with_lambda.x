-- sort with a custom comparator lambda

-- Sort integers descending
let a = [3, 1, 4, 1, 5, 9, 2, 6];
a sort(fn(x Int, y Int) { x > y }) println;

-- Sort integers ascending (same as default sort)
a sort(fn(x Int, y Int) { x < y }) println;

-- Sort floats by absolute value
let f = [3.0, -1.5, 4.2, -2.8, 0.5];
f sort(fn(x Float, y Float) { abs(x) < abs(y) }) println;

-- Sort strings by length
let s = ["hello", "hi", "hey", "howdy"];
s sort(fn(a String, b String) { a length < b length }) println;

-- Sort strings reverse alphabetical
s sort(fn(a String, b String) { a > b }) println;

-- Single element array
[42] sort(fn(x Int, y Int) { x < y }) println;

-- Empty array
let e [Int] = [];
e sort(fn(x Int, y Int) { x < y }) println;
