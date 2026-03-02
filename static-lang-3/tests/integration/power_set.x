-- Power Set (Rosetta Code)
-- Generate the power set using fold: for each element, duplicate all
-- existing subsets and add the element to the copies.

fn powerSet(arr [Int]) [[Int]] {
    let empty [Int] = [];
    arr fold([empty], fn(subsets [[Int]], x Int) [[Int]] {
        subsets $ subsets map(fn(s [Int]) [Int] { s push(x) })
    })
}

let empty [Int] = [];
powerSet(empty) println;
powerSet([1]) println;
powerSet([1, 2]) println;
powerSet([1, 2, 3]) println;
