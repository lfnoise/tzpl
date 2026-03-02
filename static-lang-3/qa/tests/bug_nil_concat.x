-- QA: BUG - nil $ List and List $ nil fail
-- The $ operator doesn't handle nil literal on either side

-- This works
let l1 = List(1, 2);
let l2 = List(3, 4);
println(l1 $ l2);

-- These now work:
println(nil $ List(1, 2));
println(List(1, 2) $ nil);

-- Workaround (no longer needed): assign nil to a typed variable first
let empty List<Int> = nil;
println(empty $ List(1, 2));
println(List(1, 2) $ empty);
