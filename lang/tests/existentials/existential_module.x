-- Phase 4: a constraint imported from another module works as `some C`,
-- including an imported composition constraint. The constraint's required
-- functions are implemented locally for the concrete types being packed.
import shapes_lib.*;

struct Circle { r Float; }
struct Square { side Int; }
fn draw(c Circle) String = "circle";
fn draw(s Square) String = "square";
fn size(c Circle) Int = 1;
fn size(s Square) Int = 4;

fn render(x some Drawable) String = draw(x);
fn info(x some DrawableSized) String = draw(x) $ "#" $ toString(size(x));

let c = Circle { r: 1.0 };
let s = Square { side: 3 };
render(c) println;       -- circle
render(s) println;       -- square
info(c) println;         -- circle#1
info(s) println;         -- square#4
