-- Higher-order operations on persistent vectors

fn add2(x Int, y Int) Int { x + y }

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

-- --- function-call auto-mapping ---
let dbl = fn(x Int) Int { x * 2 };
u dbl println;                   -- postfix call, #[2, 4, 6]
dbl(u) println;                  -- prefix call, #[2, 4, 6]
let addk = fn(x Int, k Int) Int { x + k };
u addk(100) println;             -- extra scalar arg, #[101, 102, 103]
#[-1, -2, 3] abs println;        -- builtin auto-map
#[1.5, 2.5, 3.5] floor println;  -- builtin auto-map, #[1.0, 2.0, 3.0]

-- --- explicit @ ---
(u@ + 10) println;               -- #[11, 12, 13]
(u@ * u@) println;               -- #[1, 4, 9]
dbl(u@) println;                 -- #[2, 4, 6]
u@ dbl println;                  -- #[2, 4, 6]
addk(u@, 100) println;           -- #[101, 102, 103]

-- --- deep (@@) binary-op auto-mapping over nested persistent vectors ---
(#[#[1, 2], #[3, 4]] + 1) println;                       -- #[#[2,3],#[4,5]]
(#[#[1, 2], #[3, 4]] * #[#[10, 20], #[30, 40]]) println; -- #[#[10,40],#[90,160]]
(#[#[#[1]], #[#[2]]] + 100) println;                     -- depth 3
(#[#[1, 2], #[3, 4]] @@ + 1) println;                    -- explicit @@

-- --- deep (@@) function-call auto-mapping (any depth) ---
#[#[1, 2], #[3, 4]] dbl println;            -- #[#[2,4],#[6,8]]
dbl(#[#[1, 2], #[3, 4]]) println;           -- prefix
#[#[#[1]], #[#[2]]] dbl println;            -- depth 3
#[#[1, 2], #[3, 4]] addk(100) println;      -- deep with broadcast scalar arg
#[#[1, 2], #[3, 4]] @@ dbl println;         -- explicit @@ call
#[#[-1, -2], #[3, -4]] abs println;         -- deep builtin

-- --- cartesian (@n) auto-mapping over persistent vectors ---
let c1 = #[1, 2, 3];
let c2 = #[10, 20];
(c1@1 + c2@2) println;                      -- #[#[11,21],#[12,22],#[13,23]]
(c1@2 + c2@1) println;                      -- transposed dims
add2(c1@1, c2@2) println;                   -- cartesian named call
add2(c1@2, c2@1) println;
