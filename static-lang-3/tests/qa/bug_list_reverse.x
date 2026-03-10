-- QA: ACCEPTED - reverse/toArray intentionally unavailable for Lists
-- Lists can be lazy and infinite, so these operations don't apply.
-- Use collect(n) to materialize a finite prefix into an array first.

-- Array reverse works:
[1, 2, 3] reverse println;

-- String reverse works:
"hello" reverse println;

-- List reverse: not available (lists can be infinite)
-- List(1, 2, 3) reverse println;  -- No matching overload (by design)

-- List toArray: not available (lists can be infinite)
-- List(1, 2, 3) toArray println;  -- No matching overload (by design)

-- Use collect(n) to materialize a finite prefix into an array
List(1, 2, 3) collect(3) println;
List(1, 2, 3) collect(3) reverse println;
List(1, 2, 3) collect(3) reverse toList println;
