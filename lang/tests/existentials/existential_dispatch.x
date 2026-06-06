-- Phase 3: dynamic dispatch through the witness dictionary. The same
-- `render(some Drawable)` and `describe(some Shape)` route a constraint-method
-- call to the concrete implementation carried by the packed value -- true
-- existential dispatch over heterogeneous types behind one interface.
constraint Drawable<T> = requires { draw(T) String; };
constraint Shape<T> = requires { area(T) Float; name(T) String; };

struct Circle { r Float; }
struct Square { side Int; }

fn draw(c Circle) String = "circle";
fn draw(s Square) String = "square";
fn area(c Circle) Float = 3.0 * c.r * c.r;
fn area(s Square) Float = toFloat(s.side * s.side);
fn name(c Circle) String = "circle";
fn name(s Square) String = "square";

-- Single-method dispatch.
fn render(x some Drawable) String = draw(x);

-- Two-method dispatch on the same receiver.
fn describe(x some Shape) String = name(x) $ " area=" $ toString(area(x));

let c = Circle { r: 2.0 };
let s = Square { side: 3 };

render(c) println;       -- circle
render(s) println;       -- square
describe(c) println;     -- circle area=12.0
describe(s) println;     -- square area=9.0

-- Dispatch on a let-bound existential (no fresh pack at the call site).
let d some Shape = c;
area(d) println;         -- 12.0
name(d) println;         -- circle
