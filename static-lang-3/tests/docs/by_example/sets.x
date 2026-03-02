-- Sets from Language X by Example

-- Create a set
let s = Set(1, 2, 3, 4, 5);
s println;

-- Membership and modification
let s2 = Set(1, 2, 3);
s2 contains(2) println;
s2 contains(5) println;

let s3 = s2 add(4);
s3 println;

let s4 = s2 remove(2);
s4 println;

-- Set operations
let a = Set(1, 2, 3, 4);
let b = Set(3, 4, 5, 6);
union(a, b) println;
intersection(a, b) println;
difference(a, b) println;

-- Conversion and size
let s5 = Set(3, 1, 2);
s5 length println;
s5 toArray println;
