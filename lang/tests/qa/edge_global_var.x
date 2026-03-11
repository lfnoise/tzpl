-- QA: Global mutable variables

-- Basic global var
var g = 0;
g println;    -- 0
g = 42;
g println;    -- 42

-- Global var modified in function
fn increment() {
    g = g + 1;
}
g = 0;
increment();
increment();
increment();
g println;    -- 3

-- Multiple global vars
var count = 0;
var total = 0;

fn accumulate(x Int) {
    count = count + 1;
    total = total + x;
}

accumulate(10);
accumulate(20);
accumulate(30);
count println;   -- 3
total println;    -- 60

-- Global var in loop
var sum = 0;
for (i : (1..10)) { sum = sum + i; }
sum println;   -- 55

-- Global var with different types
var gs = "hello";
gs println;
gs = "world";
gs println;

var gf = 3.14;
gf println;
gf = 2.718;
gf println;

var gb = true;
gb println;
gb = false;
gb println;
