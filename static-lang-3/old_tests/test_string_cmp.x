-- Test string comparison operators and builtin functions

-- Comparison operators
let r1 = "apple" < "banana";
println(r1)
let r2 = "banana" < "apple";
println(r2)
let r3 = "abc" <= "abc";
println(r3)
let r4 = "abc" <= "abd";
println(r4)
let r5 = "abc" > "abb";
println(r5)
let r6 = "abc" >= "abc";
println(r6)
let r7 = "abc" >= "abd";
println(r7)
let r8 = "abc" == "abc";
println(r8)
let r9 = "abc" != "abd";
println(r9)

-- length
let l1 = length("hello");
println(l1)
let l2 = length("");
println(l2)
let l3 = length("abc");
println(l3)

-- min / max
let m1 = min("apple", "banana");
println(m1)
let m2 = max("apple", "banana");
println(m2)
let m3 = min("xyz", "abc");
println(m3)
let m4 = max("xyz", "abc");
println(m4)

-- cmp
let c1 = cmp("apple", "banana");
println(c1)
let c2 = cmp("banana", "apple");
println(c2)
let c3 = cmp("abc", "abc");
println(c3)
