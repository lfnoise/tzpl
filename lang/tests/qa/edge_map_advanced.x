-- QA: Advanced map operations

-- Map construction
let m = ["a": 1, "b": 2, "c": 3];
m println;

-- Map access with Option
m["a"] println;      -- Option<Int>.some(1)
m["z"] println;      -- Option<Int>.none

-- Map get with default
m get("a", 99) println;  -- 1
m get("z", 99) println;  -- 99

-- Map put (returns new map)
let m2 = m put("d", 4);
m2["d"] println;

-- Map remove (returns new map)
let m3 = m remove("b");
m3["b"] println;     -- Option<Int>.none
m3 length println;   -- 2

-- Map keys and values
m keys println;
m values println;

-- Map length
m length println;    -- 3

-- Map merge
let x = ["a": 1, "b": 2];
let y = ["b": 20, "c": 30];
merge(x, y) println;  -- b should be 20 (y wins)

-- Map contains (checks keys)
m contains("a") println;   -- true
m contains("z") println;   -- false

-- Map with integer keys
let mi = [1: "one", 2: "two", 3: "three"];
mi[1] println;
mi[2] println;
mi length println;

-- Map with symbol keys
let ms = ['x: 10, 'y: 20];
ms['x] println;
ms['y] println;

-- Empty map
let empty [String: Int] = [:];
empty length println;  -- 0
empty["a"] println;    -- Option<Int>.none

-- Map from pairs via put chain
let empty2 [String: Int] = [:];
let built = empty2 put("a", 1) put("b", 2) put("c", 3);
built length println;  -- 3
built["b"] println;
