-- String concatenation

-- Basic $
println("hello" $ " " $ "world");
println("" $ "abc");
println("abc" $ "");
println("a" $ "b" $ "c" $ "d");

-- String with numbers (via variables)
let count = 42;
let pi = 3.14;
let msg = "count=" $ (count toString) $ " pi=" $ (pi toString);
msg println;

-- Empty string
println("" $ "");

-- Multiple concatenations
let s = "a" $ "b" $ "c" $ "d" $ "e";
s println;

-- String length after concat
length("hello" $ " " $ "world") println;

-- Concat in expressions
fn greet(name String) String = "Hi " $ name;
greet("Bob") println;
greet("Alice") println;

-- Repeat via loop
fn repeat_str(s String, n Int) String {
    var result = "";
    var i = 0;
    while (i < n) {
        result = result $ s;
        i = i + 1;
    }
    result
}
repeat_str("ab", 3) println;
repeat_str("*", 5) println;
