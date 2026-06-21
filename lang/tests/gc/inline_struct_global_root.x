-- Regression: a global (module-level) `let`/`var` of a multi-word INLINE value
-- type (a small struct whose fields pack into consecutive registers) must not
-- make the GC treat its trailing payload words as independent Obj* roots. The
-- stack-map emitters classified every live register on its own, so the trailing
-- words of the inline slot -- which carry stale obj-typed regTypes_ from a prior
-- tenant -- were listed as live refs. Once a collection ran while the global was
-- live, the GC dereferenced a Float field word (e.g. 1.0 -> 0x3ff0...0) as a
-- pointer and crashed. Fix: skip an inline value's whole span in the stack map;
-- its fields are reached via the base register's payload walk.

struct Spec { lo Float, hi Float, init Float, warp Int }
struct V3 { x Float, y Float, z Float }

-- multi-word inline-typed globals holding Float field words (1.0 packs to a
-- pointer-looking bit pattern, which is what tripped the marker).
let s = Spec { lo: 0.0, hi: 1.0, init: 0.3, warp: 2 };
let v = V3 { x: 1.0, y: 2.0, z: 3.0 };

-- Force many GC cycles while s/v are live global roots.
var round = 0;
while (round < 800) {
	var junk [String] = [];
	var j = 0;
	while (j < 50) { junk push!("padding text " $ j toString $ " words"); j = j + 1; }
	let sorted = junk sort;
	round = round + 1;
}

println(s.hi);      -- 1.0
println(s.init);    -- 0.3
println(v.x + v.y + v.z);   -- 6.0
