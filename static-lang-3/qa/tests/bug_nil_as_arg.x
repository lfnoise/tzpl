-- QA: BUG - nil literal cannot be passed where List<T> expected
-- The type of bare `nil` is `?` which doesn't match List<Int>

fn list_sum(lst List<Int>) Int {
    match (lst) {
        nil: return 0;
        h :: t: return h + list_sum(t);
    }
    0
}

-- This works:
list_sum(List(1, 2, 3)) println;

-- This works (workaround):
let empty List<Int> = nil;
list_sum(empty) println;

-- This now works:
list_sum(nil) println;
