-- Higher-order operations on persistent vectors

let v = #[1, 2, 3, 4];

-- map returns a new persistent vector
v map(fn(x Int) { x * 10 }) println;

-- filter
v filter(fn(x Int) { x > 2 }) println;

-- fold
v fold(0, fn(a Int, x Int) { a + x }) println;
v fold(1, fn(a Int, x Int) { a * x }) println;

-- map changing the element type
v map(fn(x Int) Float { x * 1.5 }) println;

-- the original is never mutated
v println;

-- map over a persistent vector built from a conversion
[10, 20, 30] toPersistentVector map(fn(x Int) { x + 1 }) println;
