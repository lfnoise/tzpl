-- Heterogeneous collections of existentials: an array and a list each holding
-- values of different concrete types behind one constraint. Elements pack on
-- construction and dispatch per-element.
constraint Shape<T> = requires { area(T) Float; name(T) String; };

struct Circle { r Float; }
struct Square { side Float; }

fn area(c Circle) Float = 3.0 * c.r * c.r;
fn area(s Square) Float = s.side * s.side;
fn name(c Circle) String = "circle";
fn name(s Square) String = "square";

let c = Circle { r: 2.0 };
let s = Square { side: 3.0 };

-- Heterogeneous array, indexed dispatch + iteration with a running total.
let shapes [some Shape] = [c, s, c];
shapes length println;          -- 3
name(shapes[1]) println;        -- square

var total = 0.0;
var i = 0;
while (i < shapes length) {
    total = total + area(shapes[i]);
    i = i + 1;
}
total println;                  -- 12 + 9 + 12 = 33

-- Heterogeneous list, head/tail dispatch.
let lst List<some Shape> = List(s, c, s);
name(lst head) println;         -- square
area(lst tail head) println;    -- 12
