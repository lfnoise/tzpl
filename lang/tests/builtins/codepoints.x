-- codePoints: String -> List[Int]

-- ASCII string
println(codePoints("ABC") collect(3));

-- Empty string returns nil list
println(codePoints("") isNil);

-- Single character
println(codePoints("Z") head);

-- Multi-byte UTF-8 (e is U+00E9, 2 bytes)
println(codePoints("é") head);

-- 3-byte UTF-8 (euro sign U+20AC)
println(codePoints("€") head);

-- 4-byte UTF-8 (U+1F600 grinning face)
println(codePoints("😀") head);

-- Mixed ASCII and multi-byte
println(codePoints("Aé€") collect(3));

-- Lazy: take first 3 from a longer string
println(codePoints("hello world") take(3) collect(3));
