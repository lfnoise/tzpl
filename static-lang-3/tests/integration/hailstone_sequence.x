-- Hailstone Sequence (Rosetta Code)
-- Iterative sequence generation using iter.

fn hailstoneStep(n Int) Int {
    if (n % 2 == 0) { n // 2 } else { 3 * n + 1 }
}

fn hailstone(n Int) List<Int> =
    iter(n, hailstoneStep) takeWhile(fn(x Int) Bool { x != 1 }) $ List(1);

-- Show the sequence for 27
let seq27 = 27 hailstone;
let len = seq27 length;
len println;
seq27 take(4) println;
seq27 drop(len - 4) println;
