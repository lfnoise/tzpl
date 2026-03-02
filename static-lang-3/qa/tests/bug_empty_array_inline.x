-- QA: Empty typed array construction
-- Correct syntax: [Int]() creates an empty Int array

-- Typed variable:
let empty [Int] = [];
empty println;
println(empty $ [1, 2, 3]);

-- Inline with constructor syntax:
println([Int]() $ [1, 2, 3]);

-- Also:
let e = [Int]();
println(e $ [1, 2, 3]);
println([1, 2, 3] $ e);
