-- Ranges and For Loops from Tzopilotl by Example

-- Basic range
let r = (1..5);
r println;
r length println;
r toArray println;

-- Stepped ranges
let evens = (0,2..10);
evens toArray println;

-- Descending ranges
let down = (5..1);
down toArray println;

-- Descending with explicit step
let odds = (9,7..1);
odds toArray println;

-- For loop over range
for (i : (1..5)) {
    print(i);
    print(" ");
}
println();

-- Stepped range loop
for (i : (0,2..10)) {
    print(i);
    print(" ");
}
println();

-- Descending range loop
for (i : (5..1)) {
    print(i);
    print(" ");
}
println();

-- Descending with explicit step loop
for (i : (10,7..1)) {
    print(i);
    print(" ");
}
println();

-- For loop over an array
let arr = [10, 20, 30];
for (x : arr) {
    print(x);
    print(" ");
}
println();

-- For loop over a list
let lst = List(1, 2, 3);
for (x : lst) {
    print(x);
    print(" ");
}
println();

-- Range builtins
let r2 = (1..5);
r2 length println;
r2 toArray println;
r2 toList println;

-- Infinite range toList (will print truncated)
let nat = (1..);
let nats = nat toList;
nats take(10) println;

-- Fraction ranges
for (i : (1/2..5/2)) {
    print(i);
    print(" ");
}
println();

-- Stepped fraction range
for (i : (1/4, 1/2..2/1)) {
    print(i);
    print(" ");
}
println();

-- Descending fraction range
for (i : (3/1..1/2)) {
    print(i);
    print(" ");
}
println();

-- Fraction range as value
let fr = (1/4, 3/4..3/1);
fr length println;
fr toArray println;
fr toList println;
