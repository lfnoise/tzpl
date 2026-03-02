-- QA: Set operations

-- Set construction
let s = Set(1, 2, 3);
s println;

-- Set with duplicates
Set(1, 1, 2, 2, 3) println;

-- Set length
Set(1, 2, 3) length println;  -- 3

-- Set contains
Set(1, 2, 3) contains(1) println;   -- true
Set(1, 2, 3) contains(5) println;   -- false

-- Set add
Set(1, 2, 3) add(4) println;
Set(1, 2, 3) add(2) println;  -- already present

-- Set remove
Set(1, 2, 3) remove(2) println;
Set(1, 2, 3) remove(5) println;  -- not present

-- Set union
union(Set(1, 2, 3), Set(2, 3, 4)) println;

-- Set intersection
intersection(Set(1, 2, 3), Set(2, 3, 4)) println;

-- Set difference
difference(Set(1, 2, 3), Set(2, 3, 4)) println;

-- Set toArray
Set(3, 1, 2) toArray sort println;

-- Set with strings
Set("a", "b", "c") contains("b") println;   -- true
Set("a", "b", "c") contains("d") println;   -- false

-- Set with symbols
Set('x, 'y, 'z) contains('y) println;   -- true
Set('x, 'y, 'z) length println;         -- 3

-- Empty set operations
let e = Set(1) remove(1);
e length println;      -- 0
e contains(1) println; -- false
