-- Template (generic) functions

-- Basic identity
fn identity<T>(x T) T = x;
42 identity println;
3.14 identity println;
"hello" identity println;

-- Same type param in multiple positions
fn max_of<T>(a T, b T) T = a > b ? a : b;
max_of(3, 7) println;
max_of(3.14, 2.71) println;

-- Template with Array parameter
fn first<T>(arr [T]) T = arr[0];
[10, 20, 30] first println;
[1.1, 2.2] first println;
["hello", "world"] first println;

-- Return type inference with templates
fn double_it<T>(x T) = x + x;
5 double_it println;
2.5 double_it println;

-- Template with auto-mapping
fn negate<T>(x T) T = -x;
5 negate println;
[1, 2, 3] negate println;

-- Template calling another template
fn abs_val<T>(x T) T = x < 0 ? -x : x;
fn abs_max<T>(a T, b T) T = max_of(abs_val(a), abs_val(b));
abs_max(-5, 3) println;
abs_max(-2.0, -7.0) println;

-- Two type parameters
fn pick_first<T, U>(a T, b U) T = a;
fn pick_second<T, U>(a T, b U) U = b;
pick_first(42, "ignored") println;
pick_second(99, "world") println;

-- Two type parameters with Array
fn first_of_first<T, U>(a [T], b [U]) T = a[0];
fn first_of_second<T, U>(a [T], b [U]) U = b[0];
first_of_first([1, 2, 3], ["a", "b", "c"]) println;
first_of_second([1, 2, 3], ["a", "b", "c"]) println;

-- Three type parameters
fn pick_third<T, U, V>(a T, b U, c V) V = c;
pick_third(1, 2.0, "three") println;

-- Tuple type annotations
fn make_pair<T, U>(a T, b U) (T, U) = (a, b);
make_pair(1, "hello") println;
make_pair(3.14, 42) println;

fn swap<T, U>(p (T, U)) (U, T) {
    let (a, b) = p;
    (b, a)
}
(1, "hello") swap println;
("world", 3.14) swap println;

-- Cache hits (same monomorphization reused)
100 identity println;
"world" identity println;
