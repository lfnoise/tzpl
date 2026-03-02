-- QA: String escape sequence edge cases

-- Newline
println("line1\nline2");

-- Tab
println("col1\tcol2");

-- Backslash
println("back\\slash");

-- Double quote
println("say \"hello\"");

-- Carriage return
println("before\rafter");

-- Null byte? (might not be supported)
-- println("null\0byte");

-- Multiple escapes
println("a\tb\tc");
println("a\nb\nc");

-- Empty string with escape-like chars
println("");

-- String with only escape chars
println("\n");
println("\t");
println("\\");

-- Long string with many escapes
println("line1\nline2\nline3\nline4");

-- String comparison with escapes
println("a\tb" == "a\tb");
println("a\tb" == "a b");

-- String length with escapes
"\n" length println;   -- should be 1
"\t" length println;   -- should be 1
"\\" length println;   -- should be 1
"\"" length println;   -- should be 1
