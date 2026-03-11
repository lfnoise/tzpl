-- grade: return indices that would sort an array

-- Basic ascending grade
let a = [30, 10, 40, 20];
a grade(fn(x Int, y Int) { x < y }) println;

-- Descending grade
a grade(fn(x Int, y Int) { x > y }) println;

-- Grade of already sorted array
let b = [1, 2, 3, 4, 5];
b grade(fn(x Int, y Int) { x < y }) println;

-- Grade floats
let f = [3.0, 1.0, 4.0, 1.5, 2.0];
f grade(fn(x Float, y Float) { x < y }) println;

-- Grade strings alphabetically
let s = ["banana", "apple", "cherry"];
s grade(fn(a String, b String) { a < b }) println;

-- Single element
[42] grade(fn(x Int, y Int) { x < y }) println;

-- Empty array
let e [Int] = [];
e grade(fn(x Int, y Int) { x < y }) println;
