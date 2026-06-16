-- Regression: the result register of a value-producing conditional (ternary /
-- if-expression, value-form if-else, value-form match) is write-only and NOT
-- live-in to any branch -- each branch assigns it as its final act. Codegen
-- builds GC stack maps from the compile-time per-register type table, and the
-- then-branch (compiled first) used to leave the result register typed as an
-- Obj* while the else-branch was compiled. A safepoint inside the else-branch
-- (e.g. a recursive call) then listed that register as a live Obj* root even
-- though, on the else path, it held an uninitialized non-pointer (here an Int
-- string-length). When a collection fired during the recursion, the GC marked
-- that Int as a pointer and crashed.
--
-- These recursive functions each branch on a count and recurse in the else
-- arm, with the result register holding an Int (a `length`) at the call site.
-- Driving heavy allocation forces collections mid-recursion; a leaked root
-- would dereference the Int and crash.

-- Ternary (IfExpr) form.
fn padLeft(s String, n Int) String = s length >= n ? s : padLeft(" " $ s, n);

-- Value-form if/else statement as the function body value.
fn padRight(s String, n Int) String {
	if (s length >= n) { s } else { padRight(s $ ".", n) }
}

-- Value-form match as the function body value.
fn banner(s String, n Int) String {
	match (s length >= n) {
		true: s;
		false: banner("*" $ s, n);
	}
}

var round = 0;
var checksum = 0;
while (round < 600) {
	let a = padLeft("x", 12);
	let b = padRight("y", 12);
	let c = banner("z", 12);
	-- Allocate transient garbage to push the collector through cycles while
	-- the padded strings (and the recursion frames that built them) are live.
	var junk [String] = [];
	var j = 0;
	while (j < 30) { junk push!("filler " $ j toString); j = j + 1; }
	checksum = checksum + a length + b length + c length + (junk sort) length;
	round = round + 1;
}
println(padLeft("x", 12) length);    -- 12
println(padRight("y", 12) length);   -- 12
println(banner("z", 12) length);     -- 12
println(checksum);                   -- 600 * (12 + 12 + 12 + 30)
