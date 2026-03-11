-- break in while loop
var i = 0;
while (i < 10) {
    if (i == 5) { break; }
    println(i);
    i = i + 1;
}
println("after while break");

-- continue in while loop
var j = 0;
while (j < 10) {
    j = j + 1;
    if (j % 2 == 0) { continue; }
    println(j);
}
println("after while continue");

-- break in for loop over range
for (x : (1..100)) {
    if (x > 5) { break; }
    println(x);
}
println("after for range break");

-- continue in for loop over range
for (x : (1..10)) {
    if (x % 3 == 0) { continue; }
    println(x);
}
println("after for range continue");

-- break in for loop over array
let arr = [10, 20, 30, 40, 50];
for (x : arr) {
    if (x == 30) { break; }
    println(x);
}
println("after for array break");

-- continue in for loop over array
for (x : arr) {
    if (x == 30) { continue; }
    println(x);
}
println("after for array continue");

-- break in for loop over list
let lst1 = List(100, 200, 300, 400, 500);
for (x : lst1) {
    if (x == 300) { break; }
    println(x);
}
println("after for list break");

-- continue in for loop over list
let lst2 = List(100, 200, 300, 400, 500);
for (x : lst2) {
    if (x == 300) { continue; }
    println(x);
}
println("after for list continue");

-- break in infinite range
var sum = 0;
for (x : (1..)) {
    if (x > 10) { break; }
    sum = sum + x;
}
println(sum);

-- nested loops: break only affects innermost
for (i : (1..3)) {
    for (j : (1..5)) {
        if (j > 2) { break; }
        println(i * 10 + j);
    }
}
println("after nested break");

-- nested loops: continue only affects innermost
for (i : (1..2)) {
    for (j : (1..5)) {
        if (j == 3) { continue; }
        println(i * 10 + j);
    }
}
println("after nested continue");
