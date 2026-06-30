-- Auto-mapping a constraint method over a *lazy* list of existentials must
-- stay lazy: the witness dispatch is applied per element on demand, so an
-- infinite source (here `cyc`, an endless cycle) can be consumed by `take` /
-- `collect` without forcing the whole list. Previously this eagerly walked the
-- list to nil and hung forever.
constraint Drawable<T> = requires { draw(T) String; };

struct Circle { r Float; }
struct Square { side Int; }
fn draw(c Circle) String = "circle";
fn draw(s Square) String = "square";

let c = Circle { r: 1.0 };
let s = Square { side: 3 };

let lst List<some Drawable> = List(c, s);

-- Implicit auto-map and explicit `@` over an infinite existential list.
lst cyc draw take(5) println;        -- List(circle, square, circle, square, circle)
(lst cyc @ draw) take(5) println;    -- List(circle, square, circle, square, circle)
lst cyc draw collect(4) println;     -- [circle, square, circle, square]

-- Array of lazy existential lists: outer array eager, inner lists stay lazy.
[lst cyc, lst cyc] @ draw @ take(3) println;  -- [List(circle, square, circle), List(circle, square, circle)]
