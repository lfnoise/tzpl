-- Phase 4g.5: inline-composite globals store their payload inline across
-- sizeWords_ consecutive globals_ slots (no per-store box).

struct Point { x Int, y Int }
struct Named { id Int, name String }

enum Shape {
    circle Float,
    rect (Int, Int),
    nothing
}

-- Inline-tuple global
let gt = (1, 2, 3);
gt println;

-- Inline-struct global, with reassignment
var gs = Point{10, 20};
gs println;
gs = Point{100, 200};
gs println;

-- Struct with String field: ARC walking on overwrite must release the old
-- name and retain the new one (otherwise the String leaks or use-after-frees).
var gn = Named{1, "alpha"};
gn println;
gn = Named{2, "beta"};
gn println;
gn = Named{3, "gamma"};
gn println;

-- Inline-enum global across cases with different payload shapes
var ge = Shape.circle(1.5);
ge println;
ge = Shape.rect((3, 4));
ge println;
ge = Shape.nothing;
ge println;
ge = Shape.circle(2.71);
ge println;

-- Read back into a function argument
fn show_pt(p Point) Void { p println; }
show_pt(gs);
show_pt(Point{7, 8});
