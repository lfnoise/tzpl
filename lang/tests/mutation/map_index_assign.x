-- m[k] = v: insert-or-update on maps.

var m = ["x": 1, "y": 2];

-- update existing key
m["x"] = 99;
m["x"] unwrap println;

-- insert new key
m["z"] = 7;
m["z"] unwrap println;
m length println;

-- update again, same key
m["x"] = 1000;
m["x"] unwrap println;
m length println;

-- integer-keyed map
var im = [1: "one"];
im[2] = "two";
im[1] = "ONE";
im[1] unwrap println;
im[2] unwrap println;
im length println;
