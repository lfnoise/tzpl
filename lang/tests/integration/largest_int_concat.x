-- Largest int from concatenated ints (Rosetta Code)
-- Recursive intToStr, recursive merge sort with custom comparator,
-- then fold to concatenate.

fn digitChar(d Int) String {
    match (d) {
        0: "0"; 1: "1"; 2: "2"; 3: "3"; 4: "4";
        5: "5"; 6: "6"; 7: "7"; 8: "8"; _: "9";
    }
}

fn intToStr(n Int) String {
    if (n < 10) { digitChar(n) }
    else { intToStr(n // 10) $ digitChar(n % 10) }
}

-- Merge sort with custom comparator (all recursive, no loops)
fn merge(a [String], b [String]) [String] {
    if (a length == 0) { b }
    else if (b length == 0) { a }
    else if (cmp(a[0] $ b[0], b[0] $ a[0]) >= 0) {
        [a[0]] $ merge(a drop(1), b)
    }
    else {
        [b[0]] $ merge(a, b drop(1))
    }
}

fn mergeSort(arr [String]) [String] {
    if (arr length <= 1) { arr }
    else {
        let mid = arr length // 2;
        merge(mergeSort(arr take(mid)), mergeSort(arr drop(mid)))
    }
}

fn largestConcat(nums [Int]) String {
    let strs = nums intToStr;
    strs mergeSort fold("", fn(a String, b String) String { a $ b })
}

largestConcat([1, 34, 3, 98, 9, 76, 45, 4]) println;
largestConcat([54, 546, 548, 60]) println;
