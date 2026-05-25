-- Auto-mapped function calls with inline-composite (struct) args and returns,
-- at single (@) and deep (@@) map depth. Regression test: deep-map calls must
-- extract multi-word composite args into wide slots and collect multi-word
-- composite results, matching the single-level paths.

struct V { x Int, y Int }
fn norm(v V) Int { v.x + v.y }
fn scaleV(v V) V { V{x: v.x * 2, y: v.y * 2} }

let row = [V{x: 1, y: 2}, V{x: 3, y: 4}];
-- composite arg, scalar return
println(norm(row @));
-- composite arg + composite return
println(scaleV(row @));

let grid = [[V{x: 1, y: 2}, V{x: 3, y: 4}], [V{x: 5, y: 6}]];
-- deep-map: composite arg, scalar return
println(norm(grid @@));
-- deep-map: composite arg + composite return
println(scaleV(grid @@));
