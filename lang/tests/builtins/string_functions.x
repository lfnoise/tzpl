-- String builtin functions

-- length
"hello" length println;
"" length println;
"abc" length println;

-- Comparison
println("apple" < "banana");
println("banana" < "apple");
println("abc" == "abc");
println("abc" != "abd");

-- min, max
println(min("apple", "banana"));
println(max("apple", "banana"));
println(min("xyz", "abc"));
println(max("xyz", "abc"));

-- cmp
println(cmp("apple", "banana"));
println(cmp("banana", "apple"));
println(cmp("abc", "abc"));

-- Concatenation
println("hello" $ " " $ "world");
println("" $ "abc");
println("abc" $ "");

-- substring
println(substring("hello world", 0, 5));
println(substring("hello world", 6, 5));
println(substring("abcdef", 2, 3));

-- contains
println(contains("hello world", "world"));
println(contains("hello world", "xyz"));
println(contains("abcdef", "cde"));
println(contains("abc", ""));

-- startsWith / endsWith
println(startsWith("hello world", "hello"));
println(startsWith("hello world", "world"));
println(endsWith("hello world", "world"));
println(endsWith("hello world", "hello"));

-- split
println(split("a,b,c", ","));
println(split("hello", ""));
println(split("one--two--three", "--"));

-- trim
println(trim("  hello  "));
println(trim("no spaces"));
println(trim("  leading"));
println(trim("trailing  "));

-- toUpper / toLower
println(toUpper("hello"));
println(toLower("HELLO"));
println(toUpper("Hello World"));
println(toLower("Hello World"));

-- replace
println(replace("hello world", "world", "there"));
println(replace("aabbcc", "bb", "XX"));
println(replace("abcabc", "abc", "x"));

-- Indexing (byte access)
let s = "hello";
println(s[0]);
println(s[1]);
println(s[4]);
