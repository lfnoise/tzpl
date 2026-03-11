fn identity<T>(x T) T = x;

fn make_pair<T, U>(a T, b U) (T, U) = (a, b);

fn first<T>(arr [T]) T = arr[0];

fn add(a Int, b Int) Int = a + b + 1000;
fn add<T>(a T, b T) T = a + b;

struct Box<T> { value T }

struct Pair<T, U> { first T, second U }

enum Option<T> { some T, none }

private fn _secret<T>(x T) T = x;
