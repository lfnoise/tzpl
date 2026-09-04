-- isEmpty: true when a collection has no elements. Works on every
-- collection length covers; lists answer in O(1) without forcing.

-- Array
[1, 2] isEmpty println;
[Int]() isEmpty println;

-- List
List(1, 2) isEmpty println;
nil isEmpty println;

-- Map
["a": 1] isEmpty println;
var m = ["a": 1];
m remove!("a");
m isEmpty println;

-- Set
Set(1) isEmpty println;
var s = Set(1);
s remove!(1);
s isEmpty println;

-- String
"hi" isEmpty println;
"" isEmpty println;

-- Range (infinite ranges are never empty)
(1..5) isEmpty println;
(5..1) isEmpty println;

-- Persistent vector
[1, 2] toPersistentVector isEmpty println;
[Int]() toPersistentVector isEmpty println;
