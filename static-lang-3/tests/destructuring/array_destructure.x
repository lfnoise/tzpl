-- Array destructuring

-- Fixed pattern
let [d, e, f] = [100, 200, 300];
d println;
e println;
f println;

-- Head/rest
let [head, ...tail] = [1, 2, 3, 4];
head println;
tail println;

-- Head/discard rest
let [first, ...] = [10, 20, 30];
first println;

-- In function body
fn sum_first_two(arr [Int]) Int {
    let [a, b, ...] = arr;
    a + b
}
[10, 20, 30] sum_first_two println;

-- Match with array patterns
fn matchArray(arr [Int]) Int {
    match (arr) {
        [1, 2, 3]: return 100;
        [head, ...tail]: return head;
        _: return -1;
    }
}
[1, 2, 3] matchArray println;
[42, 10, 20] matchArray println;
