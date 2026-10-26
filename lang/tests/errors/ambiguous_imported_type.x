-- Two wildcard imports define different types named Point: naming Point is
-- an error that names both modules (the later import used to win silently).
import same_type_a.*;
import same_type_b.*;
fn f(p Point) Float = p.x;
