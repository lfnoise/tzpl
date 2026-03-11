-- QA: Edge cases in collections (maps, sets, lists)

-- Empty map operations
let em [String: Int] = [:];
em length println;
em contains("x") println;
em keys() length println;
em values() length println;

-- Map: put then remove same key
let m = ["a": 1];
let m2 = m put("b", 2);
let m3 = m2 remove("b");
m3 length println;
m3 contains("b") println;

-- Map: overwrite existing key with put
let m4 = ["a": 1, "b": 2];
let m5 = m4 put("a", 99);
m5["a"] unwrap println;
m5 length println;

-- Map: merge with overlapping keys
let ma = ["a": 1, "b": 2];
let mb = ["b": 3, "c": 4];
let merged = merge(ma, mb);
merged["a"] unwrap println;
merged["b"] unwrap println;  -- should be 3 (from mb)
merged["c"] unwrap println;
merged length println;

-- Map: access missing key
let m6 = ["x": 1];
m6["y"] println;  -- should be Option.none

-- Map: get with default on missing key
get(m6, "y", 42) println;

-- Map: get with default on existing key
get(m6, "x", 42) println;

-- Empty set operations
let es = Set(1) remove(1);
es length println;
es contains(1) println;

-- Set: add duplicate
let s = Set(1, 2, 3);
let s2 = s add(2);
s2 length println;  -- should still be 3

-- Set: remove non-existent
let s3 = s remove(99);
s3 length println;  -- should still be 3

-- Set operations
let a = Set(1, 2, 3, 4);
let b = Set(3, 4, 5, 6);
union(a, b) length println;       -- 6
intersection(a, b) length println; -- 2
difference(a, b) length println;   -- 2 (1, 2)

-- Set: intersection of disjoint sets
let c = Set(1, 2);
let d = Set(3, 4);
intersection(c, d) length println;  -- 0

-- Set: union with self
union(a, a) length println;  -- 4

-- List: cons onto nil
let l = 1 :: nil;
l println;
l length println;

-- List: empty list operations
let empty_list List<Int> = nil;
empty_list length println;
empty_list println;

-- List: head and tail
let lst = List(1, 2, 3);
head(lst) println;
tail(lst) println;

-- List: concat with $
let l1 = List(1, 2);
let l2 = List(3, 4);
println(l1 $ l2);

-- List: take and drop
List(1, 2, 3, 4, 5) take(3) println;
List(1, 2, 3, 4, 5) take(0) println;
List(1, 2, 3, 4, 5) take(10) println;
List(1, 2, 3, 4, 5) drop(3) println;
List(1, 2, 3, 4, 5) drop(0) println;
List(1, 2, 3, 4, 5) drop(10) println;

-- List: map and filter
List(1, 2, 3) map(fn(x Int) { x * 2 }) println;
List(1, 2, 3, 4, 5) filter(fn(x Int) { x % 2 == 0 }) println;

-- Range: single element
let r = (5..5);
r length println;
r toArray println;
r toList println;
