-- getOrDefault (eager default) and getOrElse (lazy fallback from the key).

let m = ["a": 1, "b": 2];

-- getOrDefault: present key returns its value; absent key returns the default.
m getOrDefault("a", 0) println;        -- 1
m getOrDefault("z", 99) println;       -- 99

-- getOrElse: the fallback receives the key and runs only on a miss.
m getOrElse("b", fn(k String) Int { 0 }) println;       -- 2 (hit)

-- on a miss the fallback computes from the key (here: its length).
let lenFallback = fn(k String) Int { k length };
m getOrElse("missing", lenFallback) println;            -- 7

-- laziness: the fallback prints only when invoked. A hit prints nothing.
let noisy = fn(k String) Int { print("miss:"); print(k); print(" "); 0 };
m getOrElse("a", noisy) println;       -- 1 (no "miss:" printed)
m getOrElse("zz", noisy) println;      -- prints "miss:zz " then 0

-- integer-keyed map works too.
let im = [1: "one", 2: "two"];
im getOrDefault(1, "?") println;       -- one
im getOrElse(3, fn(k Int) String { "n=" $ k toString }) println;   -- n=3
