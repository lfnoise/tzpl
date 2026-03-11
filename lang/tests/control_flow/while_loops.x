-- While loops

-- Basic counting
var i = 0;
while (i < 5) {
    i println;
    i = i + 1;
}

-- Sum 1 to 10
var sum = 0;
var n = 1;
while (n <= 10) {
    sum = sum + n;
    n = n + 1;
}
sum println;

-- Nested while loops
var row = 1;
while (row <= 3) {
    var col = 1;
    while (col <= 3) {
        println(row * col);
        col = col + 1;
    }
    row = row + 1;
}

-- While with early condition
var x = 100;
while (x > 1) {
    x = x // 2;
}
x println;

-- Sum of squares
fn sum_of_squares(n Int) Int {
    var total = 0;
    var k = 1;
    while (k <= n) {
        total = total + k * k;
        k = k + 1;
    }
    total
}
1 sum_of_squares println;
5 sum_of_squares println;
10 sum_of_squares println;

-- Zero iterations
var count = 0;
while (false) {
    count = count + 1;
}
count println;
