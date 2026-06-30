-- Regression: higher-order builtins and lazy list generators must keep their
-- source collection, function, and accumulator alive across the per-element
-- user calls that can drive an incremental GC cycle. These were swept mid-loop
-- once a full mark/sweep completed (~5000+ elements), crashing with a
-- heap-use-after-free. Picking from a single-element array is deterministic
-- (always that element) so the counts/sums below are stable while still forcing
-- tens of thousands of GC-driving calls.
fn add1(x Int) Int = x + 1;
fn isEven(x Int) Bool = (x % 2) == 0;
fn addxy(a Int, b Int) Int = a + b;
fn lt(a Int, b Int) Bool = a < b;

-- collect over an infinite lazy list (forces 20000 generator steps)
let a = [7] picks collect(20000);
a length println;                       -- 20000

-- eager array higher-order at scale (source, function, accumulator pinned)
a map(add1) fold(0, addxy) println;     -- (7+1) * 20000 = 160000
a filter(isEven) length println;        -- 0  (7 is odd)
a fold(0, addxy) println;               -- 7 * 20000 = 140000
a scan(0, addxy) length println;        -- 20001
a find(isEven) println;                 -- -1
a sort(lt) length println;              -- 20000

-- lazy auto-map of a function over an infinite list, then collect
[7] picks add1 collect(20000) fold(0, addxy) println;   -- 160000

-- persistent-vector higher-order at scale
let pv = a toPersistentVector;
pv map(add1) length println;            -- 20000
pv filter(isEven) length println;       -- 0

-- lazy witness-map of a constraint method over an infinite existential list
constraint CanSpeak<T> = requires { speak(T) String };
struct Dog();
fn speak(x Dog) = "arf";
let es [some CanSpeak] = [Dog()];
es picks speak collect(20000) length println;   -- 20000
