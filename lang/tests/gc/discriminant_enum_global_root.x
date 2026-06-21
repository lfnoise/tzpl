-- Regression: a global (module-level) variable of a DiscriminantEnum type
-- (an all-no-data enum, stored as an i64 tag) must NOT be treated as an Obj*
-- GC root. The global GC flag used isObjType() (true for every enum) instead
-- of storesObjPtr() (false for a tag-stored DiscriminantEnum), so the GC
-- dereferenced the tag value as a pointer and crashed once a collection ran
-- while the global was live.

enum Rate { constant, init, reset, event, audio }
enum Color { red, green, blue }

-- globals of DiscriminantEnum type, holding tag values
var gr = Rate.event;
var gc = Color.blue;

-- Force GC cycles while gr/gc are live global roots.
var round = 0;
while (round < 800) {
	var junk [String] = [];
	var j = 0;
	while (j < 50) { junk push!("padding text " $ j toString $ " words"); j = j + 1; }
	let s = junk sort;
	round = round + 1;
}

println(gr ordinal);     -- 3
println(gc ordinal);     -- 2

-- reassigning a discriminant-enum global and surviving more GC still works
gr = Rate.audio;
var round2 = 0;
while (round2 < 400) {
	var junk2 [Int] = [];
	var k = 0;
	while (k < 80) { junk2 push!(k * k); k = k + 1; }
	round2 = round2 + 1;
}
println(gr ordinal);     -- 4
