-- Containers are passed by reference. Aliases see mutations.

var a = [1, 2, 3];
var b = a;
a push!(4);
a length println;       -- 4
b length println;       -- 4 (same object)

a[0] = 100;
b[0] println;           -- 100

-- copy gives independence
var c = a copy;
a push!(999);
a length println;       -- 5
c length println;       -- 4 (unaffected)

-- Map aliasing
var m = ["x": 1];
var n = m;
m["y"] = 2;
n length println;       -- 2

let m2 = m copy;
m["z"] = 3;
m length println;       -- 3
m2 length println;      -- 2

-- Set aliasing
var s = Set(1, 2);
var t = s;
s insert!(3);
t length println;       -- 3

let s2 = s copy;
s insert!(4);
s length println;       -- 4
s2 length println;      -- 3
