-- Repeat a String (Rosetta Code)
-- Fold over a range to accumulate string repetitions.

fn repeat(s String, n Int) String =
    (1..n) toArray fold("", fn(acc String, _ Int) String { acc $ s });

repeat("ha", 5) println;
repeat("*", 10) println;
repeat("abc", 3) println;
repeat("x", 1) println;
