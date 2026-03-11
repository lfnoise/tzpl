-- QA: String interpolation and formatting edge cases

-- Basic fmt
println("x = %^" fmt(42));
println("%^ + %^ = %^" fmt(1, 2, 3));

-- fmt with different types
println("int: %^, float: %^, bool: %^, str: %^" fmt(42, 3.14, true, "hi"));

-- fmt with array
println("arr: %^" fmt([1, 2, 3]));

-- fmt with tuple
println("tup: %^" fmt((1, "two", true)));

-- fmt with fraction
println("frac: %^" fmt(1/3));

-- fmt with complex
println("cmplx: %^" fmt(3.0 + 4i));

-- Empty format string
println("" fmt());

-- Format string with no placeholders
println("hello world" fmt());

-- Percent literal
println("100%% done" fmt());

-- Multiple %^
println("%^ %^ %^ %^ %^" fmt(1, 2, 3, 4, 5));

-- Indexed format args (%0, %1, etc.)
println("%1 before %0" fmt("second", "first"));

-- Reusing indexed args
println("%0 and %0" fmt("same"));

-- String $ concatenation
println("hello" $ " " $ "world");
println("" $ "nonempty");
println("nonempty" $ "");

-- toString on various types
println(toString(42));
println(toString(3.14));
println(toString(true));
println(toString(false));
println(toString(1/3));
println(toString([1,2,3]));
println(toString((1, "a")));
