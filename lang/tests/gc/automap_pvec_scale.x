-- Regression: auto-mapping a function over a persistent vector materializes the
-- pvec to a temporary array (op_pvec_to_array) and runs an array map loop whose
-- per-element user calls hit GC safepoints. The temp array must be a tracked GC
-- root, or it gets swept mid-loop (heap-use-after-free at ~tens of thousands of
-- elements). Picking from a single-element array is deterministic.
fn add1(x Int) Int = x + 1;
fn isOdd(x Int) Bool = (x % 2) == 1;
fn firstOf(a [Int]) Int = a[0];

let pv = ([7] picks collect(40000)) toPersistentVector;   -- #[Int], all 7s

(pv add1) length println;          -- 40000  (implicit auto-map of a scalar fn)
(pv @ add1) length println;        -- 40000  (explicit @ form)
pv filter(isOdd) length println;   -- 40000  (7 is odd)

-- one-level map over a persistent vector of arrays ([Int] elements -> Int)
fn mk(x Int) [Int] = [x, x];
let pva = (([7] picks collect(40000)) map(mk)) toPersistentVector;  -- #[[Int]]
(pva map(firstOf)) length println; -- 40000
