-- Regression: a live Obj held only in a TOP-frame register must survive an
-- incrementally-collected cycle that fires mid-loop.
--
-- The incremental root scanner locates the top frame's live registers via
-- vm.pc_ + the stack map at that pc. vm.pc_ is only meaningful if op_safepoint
-- publishes the current pc before stepping the GC; otherwise the top frame is
-- skipped and the loop accumulator below is swept while still in use (a
-- heap-use-after-free that surfaces once the loop allocates past the cycle
-- trigger). Building 5000 fresh arrays drives several full cycles.

fn build(n Int) Int {
    var cur = [0];
    var i = 1;
    while (i < n) { cur = cur push(i); i = i + 1; }
    -- cur is reachable only through this frame's register across every
    -- safepoint; if it were swept, the next `push` would fault.
    cur length
}

build(5000) println;        -- 5000

-- Same shape at the script top level (also a top-frame register).
var acc = [0];
var k = 1;
while (k < 5000) { acc = acc push(k * 2); k = k + 1; }
acc length println;         -- 5000
acc[4999] println;          -- 9998
