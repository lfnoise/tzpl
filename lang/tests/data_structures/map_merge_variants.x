-- mergeIfAbsent / mergeIfPresent: non-mutating conditional merges.

let a = ["x": 1, "y": 2];
let b = ["y": 99, "z": 3];

-- mergeIfAbsent: only keys not already in a are taken from b.
-- "y" stays 2 (present in a), "z" is added.
let absent = a mergeIfAbsent(b);
absent get("x", 0) println;        -- 1
absent get("y", 0) println;        -- 2 (kept, not overwritten)
absent get("z", 0) println;        -- 3 (added)
absent length println;             -- 3

-- mergeIfPresent: only keys already in a are overwritten from b.
-- "y" becomes 99, "z" is ignored.
let present = a mergeIfPresent(b);
present get("x", 0) println;       -- 1
present get("y", 0) println;       -- 99 (overwritten)
present contains("z") println;     -- false (not added)
present length println;            -- 2

-- inputs are untouched.
a get("y", 0) println;             -- 2
a length println;                  -- 2

-- empty b is a no-op either way.
let e = ["q": 1] remove("q");
a mergeIfAbsent(e) length println;   -- 2
a mergeIfPresent(e) length println;  -- 2
