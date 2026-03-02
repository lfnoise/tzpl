-- Operator overload on templated tuple struct
struct Vec2a<T>(T,T);
fn + <T>(a Vec2a<T>, b Vec2a<T>) Vec2a<T> = Vec2a(a.0 + b.0, a.1 + b.1);

let a = Vec2a(1, 2);
let b = Vec2a(10, 20);

a + b |> println;

-- Operator overload on non-templated tuple struct
struct Vec2b(Int, Int);

fn + (a Vec2b, b Vec2b) Vec2b = Vec2b(a.0 + b.0, a.1 + b.1);

let c = Vec2b(1, 2);
let d = Vec2b(10, 20);

c + d |> println;

-- Regular function on templated tuple struct
struct Vec2c<T>(T,T);
fn zonk<T>(a Vec2c<T>, b Vec2c<T>) Vec2c<T> = Vec2c(a.0 + b.0, a.1 + b.1);

let e = Vec2c(1, 2);
let f = Vec2c(10, 20);
zonk(e, f) |> println;
