-- Pascal's Triangle (Rosetta Code)
-- Each row is computed from the previous using array arithmetic:
-- the inner values are prev drop(1) + prev take(n-1), bookended by [1].
-- scan generates the full triangle from the first row.

fn nextRow(prev [Int]) [Int] {
    if (prev length == 1) { [1, 1] }
    else { [1] $ (prev drop(1) + prev take(prev length - 1)) $ [1] }
}

-- scan generates 10 rows of Pascal's triangle
let triangle = (1..9) toArray scan([1], fn(prev [Int], _ Int) [Int] { prev nextRow });
triangle println;
