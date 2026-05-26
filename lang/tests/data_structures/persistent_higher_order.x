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

-- --- sequence transforms ---
let w = #[3, 1, 4, 1, 5, 9, 2, 6];
w reverse println;
w take(3) println;
w drop(5) println;
w stride(2) println;
w sort println;
w sort(fn(a Int, b Int) { a > b }) println;
w grade(fn(a Int, b Int) { a < b }) println;
w scan(0, fn(a Int, x Int) { a + x }) println;
w scan1(fn(a Int, x Int) { a + x }) println;
w fold1(fn(a Int, x Int) { a + x }) println;
w find(fn(x Int) { x > 4 }) println;
w takeWhile(fn(x Int) { x < 5 }) println;
w dropWhile(fn(x Int) { x < 5 }) println;
#[1, 2] cat(#[3, 4]) println;
#[1, 2, 3] stutter(2) println;
#[#[1, 2], #[3], #[4, 5]] join println;
#[#[#[1], #[2]], #[#[3]]] flatten println;
#[10, 20, 30] zip(#[1, 2, 3]) println;
#['a, 'b, 'c] enumerate println;

-- --- binary-operator auto-mapping ---
let u = #[1, 2, 3];
(u + 1) println;            -- #[2, 3, 4]
(1 + u) println;            -- broadcast on the left
(u * 2) println;
(u + u) println;            -- elementwise
(u * #[10, 20, 30]) println;
(u + #[1.0, 2.0, 3.0]) println;  -- Int #[..] + Float #[..] promotes
(u < #[2, 2, 2]) println;        -- elementwise compare -> #[Bool]
(u == #[1, 2, 3]) println;       -- whole-vector structural equality -> Bool
((u + 1) * (u - 1)) println;     -- composition
