-- Set literals and operations

let s = Set(1, 2, 3);
s println;
s length println;
s contains(2) println;
s contains(5) println;

-- add returns new set
let s2 = s add(4);
s2 length println;
s2 contains(4) println;

-- remove returns new set
let s3 = s remove(2);
s3 contains(2) println;
s3 length println;

-- set operations
let a = Set(1, 2, 3);
let b = Set(2, 3, 4);
union(a, b) length println;
intersection(a, b) length println;
difference(a, b) length println;

-- toArray
let arr = s toArray();
arr length println;
