-- String features: unicode escapes, triple-quoted raw strings, guillemet raw strings

-- Basic escape sequences (existing)
println("hello\tworld");
println("line1\nline2");
println("back\\slash");
println("a\"quote\"b");

-- Unicode escape \uXXXX (4 hex digits)
println("\u0041");
println("\u00E9");
println("\u03B1\u03B2\u03B3");
println("\u2603");

-- Unicode escape \UXXXXXXXX (8 hex digits)
println("\U00000042");
println("\U0001F600");
println("\U0001F60E");

-- Unicode in context
println("caf\u00E9");
println("na\u00EFve");
println("price: \u00A35");

-- Triple-quoted raw strings (no escapes)
println("""hello world""");
println("""no \n escapes \t here""");
println("""contains "quotes" inside""");

-- Triple-quoted multi-line
println("""line one
line two
line three""");

-- Triple-quoted with backslashes (raw, no processing)
println("""C:\Users\test\new""");

-- Guillemet raw strings (no escapes)
println(«hello world»);
println(«no \n escapes \t here»);
println(«contains "quotes" inside»);

-- Guillemet multi-line
println(«line one
line two
line three»);

-- Guillemet with backslashes (raw, no processing)
println(«C:\Users\test\new»);

-- String length of unicode strings
length("\u00E9") println;
length("\U0001F600") println;

-- Concatenation with unicode
println("hello " $ "\u2603" $ " world");

-- Empty strings
println("""""");
println(«»);
