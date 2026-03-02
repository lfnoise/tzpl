-- Template function: identity
fn identity<T>(x T) T = x;

-- Template function: make_pair returns a tuple
fn make_pair<T, U>(a T, b U) (T, U) = (a, b);

-- Template function: first element of array
fn first<T>(arr [T]) T = arr[0];

-- Non-template overload alongside template
fn add(a Int, b Int) Int = a + b + 1000;
fn add<T>(a T, b T) T = a + b;

-- Template struct
struct Box<T> {
    value T
}

-- Template struct with two params
struct Pair<T, U> {
    first T,
    second U
}

-- Template enum
enum Option<T> {
    some T,
    none
}

-- Private template (should not be exported)
private fn _secret<T>(x T) T = x;
