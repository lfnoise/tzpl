-- String Functions from Builtin_Functions.html

-- length
println("hello" length);
println("" length);

-- min, max, cmp
println(min("apple", "banana"));
println(max("apple", "banana"));
println(cmp("apple", "banana"));

-- Comparison operators
println("abc" < "abd");
println("abc" == "abc");

-- substring
println(substring("hello world", 0, 5));
println(substring("hello world", 6, 5));
println(substring("abcdef", 2, 3));

-- contains
println(contains("hello world", "world"));
println(contains("hello world", "xyz"));
println(contains("abc", ""));

-- startsWith, endsWith
println(startsWith("hello world", "hello"));
println(startsWith("hello world", "world"));
println(endsWith("hello world", "world"));
println(endsWith("hello world", "hello"));

-- split
println(split("a,b,c", ","));
println(split("one--two--three", "--"));
println(split("hello", ""));

-- trim
println(trim("  hello  "));
println(trim("  leading"));
println(trim("trailing  "));

-- toUpper, toLower
println(toUpper("hello"));
println(toLower("HELLO"));
println(toUpper("Hello World"));
println(toLower("Hello World"));

-- replace
println(replace("hello world", "world", "there"));
println(replace("aabbcc", "bb", "XX"));
println(replace("abcabc", "abc", "x"));

-- Byte indexing
let s = "hello";
s[0] println;
s[1] println;
s[4] println;

-- isEmpty
println("" isEmpty);
println("hi" isEmpty);
