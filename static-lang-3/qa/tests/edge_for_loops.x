-- QA: For loop over different iterables

-- For over array
for (x : [10, 20, 30]) { x println; }

-- For over list
for (x : List(1, 2, 3)) { x println; }

-- For over range
for (i : (1..5)) { i println; }

-- For with break
var found = -1;
for (x : [10, 20, 30, 40]) {
    if (x == 30) { found = x; break; }
}
found println;  -- 30

-- For with continue
for (x : (1..5)) {
    if (x % 2 == 0) { continue; }
    x println;  -- 1, 3, 5
}

-- Nested for
for (i : (1..3)) {
    for (j : (1..3)) {
        if (i == j) { println(i * 10 + j); }
    }
}

-- For over empty array
var count = 0;
for (x : [Int]()) { count = count + 1; }
count println;  -- 0

-- For over empty list
for (x : List(1) tail) { count = count + 1; }
count println;  -- 0

-- For over infinite range with break
var s = 0;
for (n : (1..)) {
    if (n > 10) { break; }
    s = s + n;
}
s println;  -- 55
