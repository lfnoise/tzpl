-- Forward Difference (Rosetta Code)
-- The forward difference is: arr drop(1) - arr take(n-1).
-- Array arithmetic does element-wise subtraction.
-- Apply recursively for nth difference.

fn forwardDiff(arr [Int]) [Int] =
    arr drop(1) - arr take(arr length - 1);

fn nthDiff(arr [Int], n Int) [Int] {
    if (n == 0) { arr }
    else { nthDiff(arr forwardDiff, n - 1) }
}

let seq = [90, 47, 58, 29, 22, 32, 55, 5, 55, 73];

nthDiff(seq, 0) println;
nthDiff(seq, 1) println;
nthDiff(seq, 2) println;
nthDiff(seq, 9) println;
