-- Range tests

-- Basic integer range
let r = (1..5);
r println;
r length println;
r toArray println;

-- Descending range
let r2 = (5..1);
r2 println;
r2 length println;
r2 toArray println;

-- Stepped range
let r3 = (0,2..10);
r3 toArray println;

-- Stepped descending
let r4 = (10,8..2);
r4 toArray println;

-- Large step
let r5 = (0,7..100);
r5 toArray println;

-- toList (lazy)
(1..5) toList println;
(5..1) toList println;
(0,2..10) toList println;

-- Single element range
let r6 = (5..5);
r6 length println;
r6 toArray println;

-- Fraction ranges
for (i : (1/2..5/2)) {
    i println;
}

-- Fraction range as value
let fr = (1/2..5/2);
fr length println;
fr toArray println;

-- Fraction stepped range
let fr2 = (1/4, 1/2..2/1);
fr2 toArray println;

-- Infinite range (lazy, show first few)
let linf = (1..) toList;
linf println;
