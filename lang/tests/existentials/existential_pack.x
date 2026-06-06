-- Phase 2: packing a concrete value into `some C`. A Circle satisfies Drawable
-- (it has draw(Circle)), so it can be packed -- as a function argument and into
-- a let binding. Dispatch (calling draw on the packed value) arrives in a later
-- phase; here we confirm the value packs and round-trips through print.
constraint Drawable<T> = requires { draw(T) String };

struct Circle { r Float; }
fn draw(c Circle) String = "circle";

fn describe(x some Drawable) Void { "got a drawable" println; }

let c = Circle { r: 2.0 };
describe(c);                 -- pack as a function argument

let d some Drawable = c;     -- pack into a let binding
d println;                   -- existential prints as `some C(<value>)`
