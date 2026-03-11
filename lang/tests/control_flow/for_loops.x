-- For loops

-- Basic range
for (i : (1..5)) {
    i println;
}

-- Stepped range
for (i : (0,2..10)) {
    i println;
}

-- Descending range
for (i : (5,3..1)) {
    i println;
}

-- Descending range with inferred step
for (i : (5..1)) {
    i println;
}

-- For-loop over array
let arr = [10, 20, 30];
for (x : arr) {
    x println;
}

-- For-loop over list
let lst = List(1, 2, 3);
for (x : lst) {
    x println;
}

-- Range as value
let r = (1..5);
r println;
r length println;
r toArray println;

-- Descending range as value
let r2 = (5..1);
r2 println;
r2 length println;
r2 toArray println;

-- Stepped range as value
let r3 = (0,7..100);
r3 toArray println;

-- toList (lazy)
(1..5) toList println;
(5..1) toList println;
(0,2..10) toList println;

-- Single element range
let r4 = (5..5);
r4 length println;
r4 toArray println;
