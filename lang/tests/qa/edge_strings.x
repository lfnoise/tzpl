-- QA: Edge cases in string operations

-- Empty string operations
"" length println;
"" println;

-- String comparison edge cases
println("" == "");
println("" < "a");
println("a" < "");
println("" $ "");

-- Substring edge cases
println(substring("hello", 0, 0));
println(substring("hello", 5, 0));
println(substring("hello", 0, 5));

-- Contains edge cases
println(contains("", ""));
println(contains("hello", ""));
println(contains("", "hello"));

-- startsWith / endsWith with empty
println(startsWith("hello", ""));
println(startsWith("", "hello"));
println(startsWith("", ""));
println(endsWith("hello", ""));
println(endsWith("", "hello"));
println(endsWith("", ""));

-- split edge cases
println(split("", ","));
println(split("abc", "abc"));
println(split("aaa", "a"));

-- trim edge cases
println(trim(""));
println(trim("   "));
println(trim("no spaces"));

-- toUpper/toLower edge cases
println(toUpper(""));
println(toLower(""));
println(toUpper("123"));
println(toLower("123"));

-- replace edge cases
println(replace("abc", "", "x"));
println(replace("", "x", "y"));
println(replace("abc", "abc", ""));
println(replace("abc", "xyz", "replaced"));

-- String concatenation
println("value: " $ "42");
println("" $ "" $ "" $ "hello");

-- String comparison transitivity
let a = "apple";
let b = "banana";
let c = "cherry";
println(a < b);
println(b < c);
println(a < c);

-- Single character strings
println("a" length);
println("a"[0]);

-- String index at boundaries
let s = "hello";
println(s[0]);
println(s[4]);

-- Long string
let long = "abcdefghijklmnopqrstuvwxyz";
println(long length);
println(substring(long, 0, 26));

-- String with special characters
println("hello\nworld");
println("tab\there");
println("quote\"here");

-- String equality
println("abc" == "abc");
println("abc" == "abd");
println("abc" != "abd");
println("abc" != "abc");

-- Min/max on strings
println(min("apple", "banana"));
println(max("apple", "banana"));
println(min("a", "z"));
println(max("a", "z"));

-- cmp on strings
println(cmp("a", "b"));
println(cmp("b", "a"));
println(cmp("a", "a"));
