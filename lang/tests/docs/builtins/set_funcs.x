-- Set Functions from Builtin_Functions.html

let s = Set(1, 2, 3);
s contains(2) println;
s contains(5) println;

let a = Set(1, 2, 3, 4);
let b = Set(3, 4, 5, 6);
union(a, b) println;
intersection(a, b) println;
difference(a, b) println;

s toArray println;

-- isEmpty / clear!
var sEmptyTest = Set(1, 2);
println(sEmptyTest isEmpty);
sEmptyTest clear!;
println(sEmptyTest isEmpty);
