-- QA: BUG - Cannot overload unary negation for custom types
-- Fixed: type checker now checks for user-defined unary operator overloads.

struct Vec2 { x Float, y Float }

-- Binary minus
fn -(a Vec2, b Vec2) Vec2 = Vec2 { x: a.x - b.x, y: a.y - b.y };

-- Unary negation
fn -(v Vec2) Vec2 = Vec2 { x: -v.x, y: -v.y };

let v = Vec2{1.0, 2.0};
let zero = Vec2{0.0, 0.0};

-- Binary subtraction works
println(zero - v);    -- Vec2 { x: -1.0, y: -2.0 }

-- Unary negation overload
println(-v);          -- Vec2 { x: -1.0, y: -2.0 }
