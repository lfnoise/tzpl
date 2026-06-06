-- Auto-mapping a constraint method over a collection of existentials: the
-- method dispatches per element through each value's witness dictionary,
-- producing a collection of results. Works for arrays, lists, and persistent
-- vectors.
constraint Drawable<T> = requires { draw(T) String; };

struct Circle { r Float; }
struct Square { side Int; }
fn draw(c Circle) String = "circle";
fn draw(s Square) String = "square";

let c = Circle { r: 1.0 };
let s = Square { side: 3 };

let arr [some Drawable] = [c, s, c];
arr draw println;               -- [circle, square, circle]

let lst List<some Drawable> = List(s, c);
lst draw println;               -- List(square, circle)

let pv #[some Drawable] = #[c, s];
pv draw println;                -- #[circle, square]
