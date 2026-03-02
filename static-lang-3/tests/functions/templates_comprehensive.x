-- Basic identity
fn identity<T>(x T) T = x;
println(identity(42));
println(identity(3.14));
println(identity("hello"));

-- Same type param in multiple positions
fn max_of<T>(a T, b T) T = a > b ? a : b;
println(max_of(3, 7));
println(max_of(3.14, 2.71));

-- Non-template overload preferred
fn add(a Int, b Int) Int = a + b + 1000;
fn add<T>(a T, b T) T = a + b;
println(add(3, 4));
println(add(1.5, 2.5));

-- Template with Array parameter
fn first<T>(arr [T]) T = arr[0];
println(first([10, 20, 30]));
println(first([1.1, 2.2]));

-- Return type inference with templates
fn double_it<T>(x T) = x + x;
println(double_it(5));
println(double_it(2.5));

-- Template with auto-mapping
fn negate<T>(x T) T = -x;
println(negate(5));
println(negate([1, 2, 3]));

-- Template calling another template
fn abs_val<T>(x T) T = x < 0 ? -x : x;
fn abs_max<T>(a T, b T) T = max_of(abs_val(a), abs_val(b));
println(abs_max(-5, 3));
println(abs_max(-2.0, -7.0));

-- Cache hits (same monomorphization reused)
println(identity(100));
println(identity("world"));

-- Two type parameters: pick first or second
fn pick_first<T, U>(a T, b U) T = a;
fn pick_second<T, U>(a T, b U) U = b;
println(pick_first(42, "ignored"));
println(pick_first("hello", 3.14));
println(pick_second("ignored", 3.14));
println(pick_second(99, "world"));

-- Two type parameters with Array
fn first_of_first<T, U>(a [T], b [U]) T = a[0];
fn first_of_second<T, U>(a [T], b [U]) U = b[0];
println(first_of_first([1, 2, 3], ["a", "b", "c"]));
println(first_of_second([1, 2, 3], ["a", "b", "c"]));
println(first_of_first([1.1, 2.2], [10, 20]));
println(first_of_second([1.1, 2.2], [10, 20]));

-- Two type parameters with inferred return type
fn return_first<T, U>(a T, b U) = a;
println(return_first(42, "hello"));
println(return_first(3.14, 100));

-- Three type parameters
fn pick_third<T, U, V>(a T, b U, c V) V = c;
println(pick_third(1, 2.0, "three"));
println(pick_third("a", 1, 3.14));

-- Mixed: one param used multiple times, other used once
fn add_with_label<T, U>(a T, b T, label U) T = a + b;
println(add_with_label(3, 4, "sum"));
println(add_with_label(1.5, 2.5, 0));

-- Two type params: mixed scalar and array
fn scalar_and_array<T, U>(x T, arr [U]) U = arr[0];
println(scalar_and_array(42, ["a", "b", "c"]));
println(scalar_and_array("hello", [1, 2, 3]));

-- Tuple type annotations
fn make_pair<T, U>(a T, b U) (T, U) = (a, b);
println(make_pair(1, "hello"));
println(make_pair(3.14, 42));

fn triple<T, U, V>(a T, b U, c V) (T, U, V) = (a, b, c);
println(triple(1, 2.0, "three"));

fn swap<T, U>(p (T, U)) (U, T) {
    let (a, b) = p;
    (b, a)
}
println(swap((1, "hello")));
println(swap(("world", 3.14)));
